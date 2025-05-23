// 7zUpdate.cpp



#include "../../../../C/CpuArch.h"

#include "../../../Common/Wildcard.h"

#include "../../Common/CreateCoder.h"
#include "../../Common/LimitedStreams.h"
#include "../../Common/ProgressUtils.h"

#include "../../Compress/CopyCoder.h"

#include "../Common/ItemNameUtils.h"

#include "7zDecode.h"
#include "7zEncode.h"
#include "7zFolderInStream.h"
#include "7zHandler.h"
#include "7zOut.h"
#include "7zUpdate.h"

namespace NArchive {
namespace N7z {


#define k_X86 k_BCJ

struct CFilterMode
{
  UInt32 Id;
  UInt32 Delta;

  CFilterMode(): Id(0), Delta(0) {}

  void SetDelta()
  {
    if (Id == k_IA64)
      Delta = 16;
    else if (Id == k_ARM || Id == k_PPC || Id == k_SPARC)
      Delta = 4;
    else if (Id == k_ARMT)
      Delta = 2;
    else
      Delta = 0;
  }
};


/* ---------- PE ---------- */

#define MZ_SIG 0x5A4D

#define PE_SIG 0x00004550
#define PE_OptHeader_Magic_32 0x10B
#define PE_OptHeader_Magic_64 0x20B
#define PE_SectHeaderSize 40
#define PE_SECT_EXECUTE 0x20000000

static int Parse_EXE(const Byte *buf, size_t size, CFilterMode *filterMode)
{
  if (size < 512 || GetUi16(buf) != MZ_SIG)
    return 0;

  const Byte *p;
  UInt32 peOffset, optHeaderSize, filterId;

  peOffset = GetUi32(buf + 0x3C);
  if (peOffset >= 0x1000 || peOffset + 512 > size || (peOffset & 7) != 0)
    return 0;
  p = buf + peOffset;
  if (GetUi32(p) != PE_SIG)
    return 0;
  p += 4;
  
  switch (GetUi16(p))
  {
    case 0x014C:
    case 0x8664:  filterId = k_X86; break;
    
    /*
    IMAGE_FILE_MACHINE_ARM   0x01C0  // ARM LE
    IMAGE_FILE_MACHINE_THUMB 0x01C2  // ARM Thumb / Thumb-2 LE
    IMAGE_FILE_MACHINE_ARMNT 0x01C4  // ARM Thumb-2, LE
    Note: We use ARM filter for 0x01C2. (WinCE 5 - 0x01C2) files mostly contain ARM code (not Thumb/Thumb-2).
    */

    case 0x01C0:                            // WinCE old
    case 0x01C2:  filterId = k_ARM; break;  // WinCE new
    case 0x01C4:  filterId = k_ARMT; break; // WinRT

    case 0x0200:  filterId = k_IA64; break;
    default:  return 0;
  }

  optHeaderSize = GetUi16(p + 16);
  if (optHeaderSize > (1 << 10))
    return 0;

  p += 20; /* headerSize */

  switch (GetUi16(p))
  {
    case PE_OptHeader_Magic_32:
    case PE_OptHeader_Magic_64:
      break;
    default:
      return 0;
  }

  filterMode->Id = filterId;
  return 1;
}


/* ---------- ELF ---------- */

#define ELF_SIG 0x464C457F

#define ELF_CLASS_32  1
#define ELF_CLASS_64  2

#define ELF_DATA_2LSB 1
#define ELF_DATA_2MSB 2

static UInt16 Get16(const Byte *p, Bool be) { if (be) return (UInt16)GetBe16(p); return (UInt16)GetUi16(p); }
static UInt32 Get32(const Byte *p, Bool be) { if (be) return GetBe32(p); return GetUi32(p); }
// static UInt64 Get64(const Byte *p, Bool be) { if (be) return GetBe64(p); return GetUi64(p); }

static int Parse_ELF(const Byte *buf, size_t size, CFilterMode *filterMode)
{
  Bool /* is32, */ be;
  UInt32 filterId;

  if (size < 512 || buf[6] != 1) /* ver */
    return 0;

  if (GetUi32(buf) != ELF_SIG)
    return 0;

  switch (buf[4])
  {
    case ELF_CLASS_32: /* is32 = True; */ break;
    case ELF_CLASS_64: /* is32 = False; */ break;
    default: return 0;
  }

  switch (buf[5])
  {
    case ELF_DATA_2LSB: be = False; break;
    case ELF_DATA_2MSB: be = True; break;
    default: return 0;
  }

  switch (Get16(buf + 0x12, be))
  {
    case 3:
    case 6:
    case 62: filterId = k_X86; break;
    case 2:
    case 18:
    case 43: filterId = k_SPARC; break;
    case 20:
    case 21: if (!be) return 0; filterId = k_PPC; break;
    case 40: if ( be) return 0; filterId = k_ARM; break;
    
    /* Some IA-64 ELF exacutable have size that is not aligned for 16 bytes.
       So we don't use IA-64 filter for IA-64 ELF */
    // case 50: if ( be) return 0; filterId = k_IA64; break;

    default: return 0;
  }

  filterMode->Id = filterId;
  return 1;
}



/* ---------- Mach-O ---------- */

#define MACH_SIG_BE_32 0xCEFAEDFE
#define MACH_SIG_BE_64 0xCFFAEDFE
#define MACH_SIG_LE_32 0xFEEDFACE
#define MACH_SIG_LE_64 0xFEEDFACF

#define MACH_ARCH_ABI64 (1 << 24)
#define MACH_MACHINE_386 7
#define MACH_MACHINE_ARM 12
#define MACH_MACHINE_SPARC 14
#define MACH_MACHINE_PPC 18
#define MACH_MACHINE_PPC64 (MACH_ARCH_ABI64 | MACH_MACHINE_PPC)
#define MACH_MACHINE_AMD64 (MACH_ARCH_ABI64 | MACH_MACHINE_386)

static unsigned Parse_MACH(const Byte *buf, size_t size, CFilterMode *filterMode)
{
  UInt32 filterId, numCommands, commandsSize;

  if (size < 512)
    return 0;

  Bool /* mode64, */ be;
  switch (GetUi32(buf))
  {
    case MACH_SIG_BE_32: /* mode64 = False; */ be = True; break;
    case MACH_SIG_BE_64: /* mode64 = True;  */ be = True; break;
    case MACH_SIG_LE_32: /* mode64 = False; */ be = False; break;
    case MACH_SIG_LE_64: /* mode64 = True;  */ be = False; break;
    default: return 0;
  }

  switch (Get32(buf + 4, be))
  {
    case MACH_MACHINE_386:
    case MACH_MACHINE_AMD64: filterId = k_X86; break;
    case MACH_MACHINE_ARM:   if ( be) return 0; filterId = k_ARM; break;
    case MACH_MACHINE_SPARC: if (!be) return 0; filterId = k_SPARC; break;
    case MACH_MACHINE_PPC:
    case MACH_MACHINE_PPC64: if (!be) return 0; filterId = k_PPC; break;
    default: return 0;
  }

  numCommands = Get32(buf + 0x10, be);
  commandsSize = Get32(buf + 0x14, be);

  if (commandsSize > (1 << 24) || numCommands > (1 << 18))
    return 0;

  filterMode->Id = filterId;
  return 1;
}


/* ---------- WAV ---------- */

#define WAV_SUBCHUNK_fmt  0x20746D66
#define WAV_SUBCHUNK_data 0x61746164

#define RIFF_SIG 0x46464952

static Bool Parse_WAV(const Byte *buf, size_t size, CFilterMode *filterMode)
{
  UInt32 subChunkSize, pos;
  if (size < 0x2C)
    return False;

  if (GetUi32(buf + 0) != RIFF_SIG ||
      GetUi32(buf + 8) != 0x45564157 || // WAVE
      GetUi32(buf + 0xC) != WAV_SUBCHUNK_fmt)
    return False;
  subChunkSize = GetUi32(buf + 0x10);
  /* [0x14 = format] = 1 (PCM) */
  if (subChunkSize < 0x10 || subChunkSize > 0x12 || GetUi16(buf + 0x14) != 1)
    return False;
  
  unsigned numChannels = GetUi16(buf + 0x16);
  unsigned bitsPerSample = GetUi16(buf + 0x22);

  if ((bitsPerSample & 0x7) != 0 || bitsPerSample >= 256 || numChannels >= 256)
    return False;

  pos = 0x14 + subChunkSize;

  const int kNumSubChunksTests = 10;
  // Do we need to scan more than 3 sub-chunks?
  for (int i = 0; i < kNumSubChunksTests; i++)
  {
    if (pos + 8 > size)
      return False;
    subChunkSize = GetUi32(buf + pos + 4);
    if (GetUi32(buf + pos) == WAV_SUBCHUNK_data)
    {
      unsigned delta = numChannels * (bitsPerSample >> 3);
      if (delta >= 256)
        return False;
      filterMode->Id = k_Delta;
      filterMode->Delta = delta;
      return True;
    }
    if (subChunkSize > (1 << 16))
      return False;
    pos += subChunkSize + 8;
  }
  return False;
}

static Bool ParseFile(const Byte *buf, size_t size, CFilterMode *filterMode)
{
  filterMode->Id = 0;
  filterMode->Delta = 0;

  if (Parse_EXE(buf, size, filterMode)) return True;
  if (Parse_ELF(buf, size, filterMode)) return True;
  if (Parse_MACH(buf, size, filterMode)) return True;
  return Parse_WAV(buf, size, filterMode);
}




struct CFilterMode2: public CFilterMode
{
  bool Encrypted;
  unsigned GroupIndex;
  
  CFilterMode2(): Encrypted(false) {}

  int Compare(const CFilterMode2 &m) const
  {
    if (!Encrypted)
    {
      if (m.Encrypted)
        return -1;
    }
    else if (!m.Encrypted)
      return 1;
    
    if (Id < m.Id) return -1;
    if (Id > m.Id) return 1;

    if (Delta < m.Delta) return -1;
    if (Delta > m.Delta) return 1;

    return 0;
  }
  
  bool operator ==(const CFilterMode2 &m) const
  {
    return Id == m.Id && Delta == m.Delta && Encrypted == m.Encrypted;
  }
};

static unsigned GetGroup(CRecordVector<CFilterMode2> &filters, const CFilterMode2 &m)
{
  unsigned i;
  for (i = 0; i < filters.Size(); i++)
  {
    const CFilterMode2 &m2 = filters[i];
    if (m == m2)
      return i;
    /*
    if (m.Encrypted != m2.Encrypted)
    {
      if (!m.Encrypted)
        break;
      continue;
    }
    
    if (m.Id < m2.Id)  break;
    if (m.Id != m2.Id) continue;

    if (m.Delta < m2.Delta) break;
    if (m.Delta != m2.Delta) continue;
    */
  }
  // filters.Insert(i, m);
  // return i;
  return filters.Add(m);
}

static inline bool Is86Filter(CMethodId m)
{
  return (m == k_BCJ || m == k_BCJ2);
}

static inline bool IsExeFilter(CMethodId m)
{
  switch (m)
  {
    case k_BCJ:
    case k_BCJ2:
    case k_ARM:
    case k_ARMT:
    case k_PPC:
    case k_SPARC:
    case k_IA64:
      return true;
  }
  return false;
}

static unsigned Get_FilterGroup_for_Folder(
    CRecordVector<CFilterMode2> &filters, const CFolderEx &f, bool extractFilter)
{
  CFilterMode2 m;
  m.Id = 0;
  m.Delta = 0;
  m.Encrypted = f.IsEncrypted();

  if (extractFilter)
  {
    const CCoderInfo &coder = f.Coders[f.UnpackCoder];
  
    if (coder.MethodID == k_Delta)
    {
      if (coder.Props.Size() == 1)
      {
        m.Delta = (unsigned)coder.Props[0] + 1;
        m.Id = k_Delta;
      }
    }
    else if (IsExeFilter(coder.MethodID))
    {
      m.Id = (UInt32)coder.MethodID;
      if (m.Id == k_BCJ2)
        m.Id = k_BCJ;
      m.SetDelta();
    }
  }
  
  return GetGroup(filters, m);
}




static HRESULT WriteRange(IInStream *inStream, ISequentialOutStream *outStream,
    UInt64 position, UInt64 size, ICompressProgressInfo *progress)
{
  RINOK(inStream->Seek(position, STREAM_SEEK_SET, 0));
  CLimitedSequentialInStream *streamSpec = new CLimitedSequentialInStream;
  CMyComPtr<CLimitedSequentialInStream> inStreamLimited(streamSpec);
  streamSpec->SetStream(inStream);
  streamSpec->Init(size);

  NCompress::CCopyCoder *copyCoderSpec = new NCompress::CCopyCoder;
  CMyComPtr<ICompressCoder> copyCoder = copyCoderSpec;
  RINOK(copyCoder->Code(inStreamLimited, outStream, NULL, NULL, progress));
  return (copyCoderSpec->TotalSize == size ? S_OK : E_FAIL);
}

/*
unsigned CUpdateItem::GetExtensionPos() const
{
  int slashPos = Name.ReverseFind_PathSepar();
  int dotPos = Name.ReverseFind_Dot();
  if (dotPos <= slashPos)
    return Name.Len();
  return dotPos + 1;
}

UString CUpdateItem::GetExtension() const
{
  return Name.Ptr(GetExtensionPos());
}
*/

#define RINOZ(x) { int __tt = (x); if (__tt != 0) return __tt; }

#define RINOZ_COMP(a, b) RINOZ(MyCompare(a, b))

/*
static int CompareBuffers(const CByteBuffer &a1, const CByteBuffer &a2)
{
  size_t c1 = a1.GetCapacity();
  size_t c2 = a2.GetCapacity();
  RINOZ_COMP(c1, c2);
  for (size_t i = 0; i < c1; i++)
    RINOZ_COMP(a1[i], a2[i]);
  return 0;
}

static int CompareCoders(const CCoderInfo &c1, const CCoderInfo &c2)
{
  RINOZ_COMP(c1.NumInStreams, c2.NumInStreams);
  RINOZ_COMP(c1.NumOutStreams, c2.NumOutStreams);
  RINOZ_COMP(c1.MethodID, c2.MethodID);
  return CompareBuffers(c1.Props, c2.Props);
}

static int CompareBonds(const CBond &b1, const CBond &b2)
{
  RINOZ_COMP(b1.InIndex, b2.InIndex);
  return MyCompare(b1.OutIndex, b2.OutIndex);
}

static int CompareFolders(const CFolder &f1, const CFolder &f2)
{
  int s1 = f1.Coders.Size();
  int s2 = f2.Coders.Size();
  RINOZ_COMP(s1, s2);
  int i;
  for (i = 0; i < s1; i++)
    RINOZ(CompareCoders(f1.Coders[i], f2.Coders[i]));
  s1 = f1.Bonds.Size();
  s2 = f2.Bonds.Size();
  RINOZ_COMP(s1, s2);
  for (i = 0; i < s1; i++)
    RINOZ(CompareBonds(f1.Bonds[i], f2.Bonds[i]));
  return 0;
}
*/

/*
static int CompareFiles(const CFileItem &f1, const CFileItem &f2)
{
  return CompareFileNames(f1.Name, f2.Name);
}
*/

struct CFolderRepack
{
  unsigned FolderIndex;
  CNum NumCopyFiles;
};

/*
static int CompareFolderRepacks(const CFolderRepack *p1, const CFolderRepack *p2, void *)
{
  int i1 = p1->FolderIndex;
  int i2 = p2->FolderIndex;
  // In that version we don't want to parse folders here, so we don't compare folders
  // probably it must be improved in future
  // const CDbEx &db = *(const CDbEx *)param;
  // RINOZ(CompareFolders(
  //     db.Folders[i1],
  //     db.Folders[i2]));

  return MyCompare(i1, i2);
  
  // RINOZ_COMP(
  //     db.NumUnpackStreamsVector[i1],
  //     db.NumUnpackStreamsVector[i2]);
  // if (db.NumUnpackStreamsVector[i1] == 0)
  //   return 0;
  // return CompareFiles(
  //     db.Files[db.FolderStartFileIndex[i1]],
  //     db.Files[db.FolderStartFileIndex[i2]]);
}
*/

/*
  we sort empty files and dirs in such order:
  - Dir.NonAnti   (name sorted)
  - File.NonAnti  (name sorted)
  - File.Anti     (name sorted)
  - Dir.Anti (reverse name sorted)
*/

static int CompareEmptyItems(const unsigned *p1, const unsigned *p2, void *param)
{
  const CObjectVector<CUpdateItem> &updateItems = *(const CObjectVector<CUpdateItem> *)param;
  const CUpdateItem &u1 = updateItems[*p1];
  const CUpdateItem &u2 = updateItems[*p2];
  // NonAnti < Anti
  if (u1.IsAnti != u2.IsAnti)
    return (u1.IsAnti ? 1 : -1);
  if (u1.IsDir != u2.IsDir)
  {
    // Dir.NonAnti < File < Dir.Anti
    if (u1.IsDir)
      return (u1.IsAnti ? 1 : -1);
    return (u2.IsAnti ? -1 : 1);
  }
  int n = CompareFileNames(u1.Name, u2.Name);
  return (u1.IsDir && u1.IsAnti) ? -n : n;
}

static const char *g_Exts =
  " 7z xz lzma ace arc arj bz tbz bz2 tbz2 cab deb gz tgz ha lha lzh lzo lzx pak rar rpm sit zoo"
  " zip jar ear war msi"
  " 3gp avi mov mpeg mpg mpe wmv"
  " aac ape fla flac la mp3 m4a mp4 ofr ogg pac ra rm rka shn swa tta wv wma wav"
  " swf"
  " chm hxi hxs"
  " gif jpeg jpg jp2 png tiff  bmp ico psd psp"
  " awg ps eps cgm dxf svg vrml wmf emf ai md"
  " cad dwg pps key sxi"
  " max 3ds"
  " iso bin nrg mdf img pdi tar cpio xpi"
  " vfd vhd vud vmc vsv"
  " vmdk dsk nvram vmem vmsd vmsn vmss vmtm"
  " inl inc idl acf asa"
  " h hpp hxx c cpp cxx m mm go swift"
  " rc java cs rs pas bas vb cls ctl frm dlg def"
  " f77 f f90 f95"
  " asm s"
  " sql manifest dep"
  " mak clw csproj vcproj sln dsp dsw"
  " class"
  " bat cmd bash sh"
  " xml xsd xsl xslt hxk hxc htm html xhtml xht mht mhtml htw asp aspx css cgi jsp shtml"
  " awk sed hta js json php php3 php4 php5 phptml pl pm py pyo rb tcl ts vbs"
  " text txt tex ans asc srt reg ini doc docx mcw dot rtf hlp xls xlr xlt xlw ppt pdf"
  " sxc sxd sxi sxg sxw stc sti stw stm odt ott odg otg odp otp ods ots odf"
  " abw afp cwk lwp wpd wps wpt wrf wri"
  " abf afm bdf fon mgf otf pcf pfa snf ttf"
  " dbf mdb nsf ntf wdb db fdb gdb"
  " exe dll ocx vbx sfx sys tlb awx com obj lib out o so"
  " pdb pch idb ncb opt";

static unsigned GetExtIndex(const char *ext)
{
  unsigned extIndex = 1;
  const char *p = g_Exts;
  for (;;)
  {
    char c = *p++;
    if (c == 0)
      return extIndex;
    if (c == ' ')
      continue;
    unsigned pos = 0;
    for (;;)
    {
      char c2 = ext[pos++];
      if (c2 == 0 && (c == 0 || c == ' '))
        return extIndex;
      if (c != c2)
        break;
      c = *p++;
    }
    extIndex++;
    for (;;)
    {
      if (c == 0)
        return extIndex;
      if (c == ' ')
        break;
      c = *p++;
    }
  }
}

struct CRefItem
{
  const CUpdateItem *UpdateItem;
  UInt32 Index;
  unsigned ExtensionPos;
  unsigned NamePos;
  unsigned ExtensionIndex;
  
  CRefItem() {};
  CRefItem(UInt32 index, const CUpdateItem &ui, bool sortByType):
    UpdateItem(&ui),
    Index(index),
    ExtensionPos(0),
    NamePos(0),
    ExtensionIndex(0)
  {
    if (sortByType)
    {
      int slashPos = ui.Name.ReverseFind_PathSepar();
      NamePos = slashPos + 1;
      int dotPos = ui.Name.ReverseFind_Dot();
      if (dotPos <= slashPos)
        ExtensionPos = ui.Name.Len();
      else
      {
        ExtensionPos = dotPos + 1;
        if (ExtensionPos != ui.Name.Len())
        {
          AString s;
          for (unsigned pos = ExtensionPos;; pos++)
          {
            wchar_t c = ui.Name[pos];
            if (c >= 0x80)
              break;
            if (c == 0)
            {
              ExtensionIndex = GetExtIndex(s);
              break;
            }
            s += (char)MyCharLower_Ascii((char)c);
          }
        }
      }
    }
  }
};

struct CSortParam
{
  // const CObjectVector<CTreeFolder> *TreeFolders;
  bool SortByType;
};

/*
  we sort files in such order:
  - Dir.NonAnti   (name sorted)
  - alt streams
  - Dirs
  - Dir.Anti (reverse name sorted)
*/


static int CompareUpdateItems(const CRefItem *p1, const CRefItem *p2, void *param)
{
  const CRefItem &a1 = *p1;
  const CRefItem &a2 = *p2;
  const CUpdateItem &u1 = *a1.UpdateItem;
  const CUpdateItem &u2 = *a2.UpdateItem;

  /*
  if (u1.IsAltStream != u2.IsAltStream)
    return u1.IsAltStream ? 1 : -1;
  */
  
  // Actually there are no dirs that time. They were stored in other steps
  // So that code is unused?
  if (u1.IsDir != u2.IsDir)
    return u1.IsDir ? 1 : -1;
  if (u1.IsDir)
  {
    if (u1.IsAnti != u2.IsAnti)
      return (u1.IsAnti ? 1 : -1);
    int n = CompareFileNames(u1.Name, u2.Name);
    return -n;
  }
  
  // bool sortByType = *(bool *)param;
  const CSortParam *sortParam = (const CSortParam *)param;
  bool sortByType = sortParam->SortByType;
  if (sortByType)
  {
    RINOZ_COMP(a1.ExtensionIndex, a2.ExtensionIndex);
    RINOZ(CompareFileNames(u1.Name.Ptr(a1.ExtensionPos), u2.Name.Ptr(a2.ExtensionPos)));
    RINOZ(CompareFileNames(u1.Name.Ptr(a1.NamePos), u2.Name.Ptr(a2.NamePos)));
    if (!u1.MTimeDefined && u2.MTimeDefined) return 1;
    if (u1.MTimeDefined && !u2.MTimeDefined) return -1;
    if (u1.MTimeDefined && u2.MTimeDefined) RINOZ_COMP(u1.MTime, u2.MTime);
    RINOZ_COMP(u1.Size, u2.Size);
  }
  /*
  int par1 = a1.UpdateItem->ParentFolderIndex;
  int par2 = a2.UpdateItem->ParentFolderIndex;
  const CTreeFolder &tf1 = (*sortParam->TreeFolders)[par1];
  const CTreeFolder &tf2 = (*sortParam->TreeFolders)[par2];

  int b1 = tf1.SortIndex, e1 = tf1.SortIndexEnd;
  int b2 = tf2.SortIndex, e2 = tf2.SortIndexEnd;
  if (b1 < b2)
  {
    if (e1 <= b2)
      return -1;
    // p2 in p1
    int par = par2;
    for (;;)
    {
      const CTreeFolder &tf = (*sortParam->TreeFolders)[par];
      par = tf.Parent;
      if (par == par1)
      {
        RINOZ(CompareFileNames(u1.Name, tf.Name));
        break;
      }
    }
  }
  else if (b2 < b1)
  {
    if (e2 <= b1)
      return 1;
    // p1 in p2
    int par = par1;
    for (;;)
    {
      const CTreeFolder &tf = (*sortParam->TreeFolders)[par];
      par = tf.Parent;
      if (par == par2)
      {
        RINOZ(CompareFileNames(tf.Name, u2.Name));
        break;
      }
    }
  }
  */
  // RINOZ_COMP(a1.UpdateItem->ParentSortIndex, a2.UpdateItem->ParentSortIndex);
  RINOK(CompareFileNames(u1.Name, u2.Name));
  RINOZ_COMP(a1.UpdateItem->IndexInClient, a2.UpdateItem->IndexInClient);
  RINOZ_COMP(a1.UpdateItem->IndexInArchive, a2.UpdateItem->IndexInArchive);
  return 0;
}

struct CSolidGroup
{
  CRecordVector<UInt32> Indices;

  CRecordVector<CFolderRepack> folderRefs;
};

static const char * const g_ExeExts[] =
{
    "dll"
  , "exe"
  , "ocx"
  , "sfx"
  , "sys"
};

static bool IsExeExt(const wchar_t *ext)
{
  for (unsigned i = 0; i < ARRAY_SIZE(g_ExeExts); i++)
    if (StringsAreEqualNoCase_Ascii(ext, g_ExeExts[i]))
      return true;
  return false;
}

struct CAnalysis
{
  CMyComPtr<IArchiveUpdateCallbackFile> Callback;
  CByteBuffer Buffer;

  bool ParseWav;
  bool ParseExe;
  bool ParseAll;

  CAnalysis():
      ParseWav(true),
      ParseExe(false),
      ParseAll(false)
  {}

  HRESULT GetFilterGroup(UInt32 index, const CUpdateItem &ui, CFilterMode &filterMode);
};

static const size_t kAnalysisBufSize = 1 << 14;

HRESULT CAnalysis::GetFilterGroup(UInt32 index, const CUpdateItem &ui, CFilterMode &filterMode)
{
  filterMode.Id = 0;
  filterMode.Delta = 0;

  CFilterMode filterModeTemp = filterMode;

  int slashPos = ui.Name.ReverseFind_PathSepar();
  int dotPos = ui.Name.ReverseFind_Dot();

  // if (dotPos > slashPos)
  {
    bool needReadFile = ParseAll;

    bool probablyIsSameIsa = false;

    if (!needReadFile || !Callback)
    {
      const wchar_t *ext;
      if (dotPos > slashPos)
        ext = ui.Name.Ptr(dotPos + 1);
      else
        ext = ui.Name.RightPtr(0);
      
      // p7zip uses the trick to store posix attributes in high 16 bits
      if (ui.Attrib & 0x8000)
      {
        unsigned st_mode = ui.Attrib >> 16;
        // st_mode = 00111;
        if ((st_mode & 00111) && (ui.Size >= 2048))
        {
          #ifndef _WIN32
          probablyIsSameIsa = true;
          #endif
          needReadFile = true;
        }
      }

      if (IsExeExt(ext))
      {
        needReadFile = true;
        #ifdef _WIN32
        probablyIsSameIsa = true;
        needReadFile = ParseExe;
        #endif
      }
      else if (StringsAreEqualNoCase_Ascii(ext, "wav"))
      {
        needReadFile = ParseWav;
      }
      /*
      else if (!needReadFile && ParseUnixExt)
      {
        if (StringsAreEqualNoCase_Ascii(ext, "so")
          || StringsAreEqualNoCase_Ascii(ext, ""))
          
          needReadFile = true;
      }
      */
    }

    if (needReadFile && Callback)
    {
      if (Buffer.Size() != kAnalysisBufSize)
      {
        Buffer.Alloc(kAnalysisBufSize);
      }
      {
        CMyComPtr<ISequentialInStream> stream;
        HRESULT result = Callback->GetStream2(index, &stream, NUpdateNotifyOp::kAnalyze);
        if (result == S_OK && stream)
        {
          size_t size = kAnalysisBufSize;
          result = ReadStream(stream, Buffer, &size);
          stream.Release();
          // RINOK(Callback->SetOperationResult2(index, NUpdate::NOperationResult::kOK));
          if (result == S_OK)
          {
            Bool parseRes = ParseFile(Buffer, size, &filterModeTemp);
            if (parseRes && filterModeTemp.Delta == 0)
            {
              filterModeTemp.SetDelta();
              if (filterModeTemp.Delta != 0 && filterModeTemp.Id != k_Delta)
              {
                if (ui.Size % filterModeTemp.Delta != 0)
                {
                  parseRes = false;
                }
              }
            }
            if (!parseRes)
            {
              filterModeTemp.Id = 0;
              filterModeTemp.Delta = 0;
            }
          }
        }
      }
    }
    else if ((needReadFile && !Callback) || probablyIsSameIsa)
    {
      #ifdef MY_CPU_X86_OR_AMD64
      if (probablyIsSameIsa)
        filterModeTemp.Id = k_X86;
      #endif
    }
  }
  
  filterMode = filterModeTemp;
  return S_OK;
}

static inline void GetMethodFull(UInt64 methodID, UInt32 numStreams, CMethodFull &m)
{
  m.Id = methodID;
  m.NumStreams = numStreams;
}

static HRESULT AddBondForFilter(CCompressionMethodMode &mode)
{
  for (unsigned c = 1; c < mode.Methods.Size(); c++)
  {
    if (!mode.IsThereBond_to_Coder(c))
    {
      CBond2 bond;
      bond.OutCoder = 0;
      bond.OutStream = 0;
      bond.InCoder = c;
      mode.Bonds.Add(bond);
      return S_OK;
    }
  }
  return E_INVALIDARG;
}

static HRESULT AddFilterBond(CCompressionMethodMode &mode)
{
  if (!mode.Bonds.IsEmpty())
    return AddBondForFilter(mode);
  return S_OK;
}

static HRESULT AddBcj2Methods(CCompressionMethodMode &mode)
{
  // mode.Methods[0] must be k_BCJ2 method !

  CMethodFull m;
  GetMethodFull(k_LZMA, 1, m);
  
  m.AddProp32(NCoderPropID::kDictionarySize, 1 << 20);
  m.AddProp32(NCoderPropID::kNumFastBytes, 128);
  m.AddProp32(NCoderPropID::kNumThreads, 1);
  m.AddProp32(NCoderPropID::kLitPosBits, 2);
  m.AddProp32(NCoderPropID::kLitContextBits, 0);
  // m.AddProp_Ascii(NCoderPropID::kMatchFinder, "BT2");

  unsigned methodIndex = mode.Methods.Size();

  if (mode.Bonds.IsEmpty())
  {
    for (unsigned i = 1; i + 1 < mode.Methods.Size(); i++)
    {
      CBond2 bond;
      bond.OutCoder = i;
      bond.OutStream = 0;
      bond.InCoder = i + 1;
      mode.Bonds.Add(bond);
    }
  }

  mode.Methods.Add(m);
  mode.Methods.Add(m);
  
  RINOK(AddBondForFilter(mode));
  CBond2 bond;
  bond.OutCoder = 0;
  bond.InCoder = methodIndex;      bond.OutStream = 1;  mode.Bonds.Add(bond);
  bond.InCoder = methodIndex + 1;  bond.OutStream = 2;  mode.Bonds.Add(bond);
  return S_OK;
}

static HRESULT MakeExeMethod(CCompressionMethodMode &mode,
    const CFilterMode &filterMode, /* bool addFilter, */ bool bcj2Filter)
{
  if (mode.Filter_was_Inserted)
  {
    const CMethodFull &m = mode.Methods[0];
    CMethodId id = m.Id;
    if (id == k_BCJ2)
      return AddBcj2Methods(mode);
    if (!m.IsSimpleCoder())
      return E_NOTIMPL;
    // if (Bonds.IsEmpty()) we can create bonds later
    return AddFilterBond(mode);
  }

  if (filterMode.Id == 0)
    return S_OK;

  CMethodFull &m = mode.Methods.InsertNew(0);

  {
    FOR_VECTOR(k, mode.Bonds)
    {
      CBond2 &bond = mode.Bonds[k];
      bond.InCoder++;
      bond.OutCoder++;
    }
  }

  HRESULT res;
  
  if (bcj2Filter && Is86Filter(filterMode.Id))
  {
    GetMethodFull(k_BCJ2, 4, m);
    res = AddBcj2Methods(mode);
  }
  else
  {
    GetMethodFull(filterMode.Id, 1, m);
    if (filterMode.Id == k_Delta)
      m.AddProp32(NCoderPropID::kDefaultProp, filterMode.Delta);
    res = AddFilterBond(mode);

    int alignBits = -1;
    if (filterMode.Id == k_Delta || filterMode.Delta != 0)
    {
           if (filterMode.Delta == 1) alignBits = 0;
      else if (filterMode.Delta == 2) alignBits = 1;
      else if (filterMode.Delta == 4) alignBits = 2;
      else if (filterMode.Delta == 8) alignBits = 3;
      else if (filterMode.Delta == 16) alignBits = 4;
    }
    else
    {
      // alignBits = GetAlignForFilterMethod(filterMode.Id);
    }
    
    if (res == S_OK && alignBits >= 0)
    {
      unsigned nextCoder = 1;
      if (!mode.Bonds.IsEmpty())
      {
        nextCoder = mode.Bonds.Back().InCoder;
      }
      if (nextCoder < mode.Methods.Size())
      {
        CMethodFull &nextMethod = mode.Methods[nextCoder];
        if (nextMethod.Id == k_LZMA || nextMethod.Id == k_LZMA2)
        {
          if (!nextMethod.Are_Lzma_Model_Props_Defined())
          {
            if (alignBits != 0)
            {
              if (alignBits > 2 || filterMode.Id == k_Delta)
                nextMethod.AddProp32(NCoderPropID::kPosStateBits, alignBits);
              unsigned lc = 0;
              if (alignBits < 3)
                lc = 3 - alignBits;
              nextMethod.AddProp32(NCoderPropID::kLitContextBits, lc);
              nextMethod.AddProp32(NCoderPropID::kLitPosBits, alignBits);
            }
          }
        }
      }
    }
  }

  return res;
}


static void FromUpdateItemToFileItem(const CUpdateItem &ui,
    CFileItem &file, CFileItem2 &file2)
{
  if (ui.AttribDefined)
    file.SetAttrib(ui.Attrib);
  
  file2.CTime = ui.CTime;  file2.CTimeDefined = ui.CTimeDefined;
  file2.ATime = ui.ATime;  file2.ATimeDefined = ui.ATimeDefined;
  file2.MTime = ui.MTime;  file2.MTimeDefined = ui.MTimeDefined;
  file2.IsAnti = ui.IsAnti;
  // file2.IsAux = false;
  file2.StartPosDefined = false;

  file.Size = ui.Size;
  file.IsDir = ui.IsDir;
  file.HasStream = ui.HasStream();
  // file.IsAltStream = ui.IsAltStream;
}

class CRepackInStreamWithSizes:
  public ISequentialInStream,
  public ICompressGetSubStreamSize,
  public CMyUnknownImp
{
  CMyComPtr<ISequentialInStream> _stream;
  // UInt64 _size;
  const CBoolVector *_extractStatuses;
  UInt32 _startIndex;
public:
  const CDbEx *_db;

  void Init(ISequentialInStream *stream, UInt32 startIndex, const CBoolVector *extractStatuses)
  {
    _startIndex = startIndex;
    _extractStatuses = extractStatuses;
    // _size = 0;
    _stream = stream;
  }
  // UInt64 GetSize() const { return _size; }

  MY_UNKNOWN_IMP2(ISequentialInStream, ICompressGetSubStreamSize)

  STDMETHOD(Read)(void *data, UInt32 size, UInt32 *processedSize);

  STDMETHOD(GetSubStreamSize)(UInt64 subStream, UInt64 *value);
};

STDMETHODIMP CRepackInStreamWithSizes::Read(void *data, UInt32 size, UInt32 *processedSize)
{
  return _stream->Read(data, size, processedSize);
  /*
  UInt32 realProcessedSize;
  HRESULT result = _stream->Read(data, size, &realProcessedSize);
  _size += realProcessedSize;
  if (processedSize)
    *processedSize = realProcessedSize;
  return result;
  */
}

STDMETHODIMP CRepackInStreamWithSizes::GetSubStreamSize(UInt64 subStream, UInt64 *value)
{
  *value = 0;
  if (subStream >= _extractStatuses->Size())
    return S_FALSE; // E_FAIL;
  unsigned index = (unsigned)subStream;
  if ((*_extractStatuses)[index])
  {
    const CFileItem &fi = _db->Files[_startIndex + index];
    if (fi.HasStream)
      *value = fi.Size;
  }
  return S_OK;
}


class CRepackStreamBase
{
protected:
  bool _needWrite;
  bool _fileIsOpen;
  bool _calcCrc;
  UInt32 _crc;
  UInt64 _rem;

  const CBoolVector *_extractStatuses;
  UInt32 _startIndex;
  unsigned _currentIndex;

  HRESULT OpenFile();
  HRESULT CloseFile();
  HRESULT ProcessEmptyFilesOrDirectory();

public:
  const CDbEx *_db;
  CMyComPtr<IArchiveUpdateCallbackFile> _opCallback;
  CMyComPtr<IArchiveExtractCallbackMessage> _extractCallback;

  HRESULT Init(UInt32 startIndex, const CBoolVector *extractStatuses);
  HRESULT CheckFinishedState() const { return (_currentIndex == _extractStatuses->Size()) ? S_OK: E_FAIL; }
};

HRESULT CRepackStreamBase::Init(UInt32 startIndex, const CBoolVector *extractStatuses)
{
  _startIndex = startIndex;
  _extractStatuses = extractStatuses;

  _currentIndex = 0;
  _fileIsOpen = false;
  
  return ProcessEmptyFilesOrDirectory();
}

HRESULT CRepackStreamBase::OpenFile()
{
  UInt32 arcIndex = _startIndex + _currentIndex;
  const CFileItem &fi = _db->Files[arcIndex];
  
  _needWrite = (*_extractStatuses)[_currentIndex];
  if (_opCallback)
  {
    RINOK(_opCallback->ReportOperation(
        NEventIndexType::kInArcIndex, arcIndex,
        _needWrite ?
            NUpdateNotifyOp::kRepack :
            NUpdateNotifyOp::kSkip));
  }

  _crc = CRC_INIT_VAL;
  _calcCrc = (fi.CrcDefined && !fi.IsDir);

  _fileIsOpen = true;
  _rem = fi.Size;
  return S_OK;
}

const HRESULT k_My_HRESULT_CRC_ERROR = 0x20000002;

HRESULT CRepackStreamBase::CloseFile()
{
  UInt32 arcIndex = _startIndex + _currentIndex;
  const CFileItem &fi = _db->Files[arcIndex];
  _fileIsOpen = false;
  _currentIndex++;
  if (!_calcCrc || fi.Crc == CRC_GET_DIGEST(_crc))
    return S_OK;

  if (_extractCallback)
  {
    RINOK(_extractCallback->ReportExtractResult(
        NEventIndexType::kInArcIndex, arcIndex,
        NExtract::NOperationResult::kCRCError));
  }
  // return S_FALSE;
  return k_My_HRESULT_CRC_ERROR;
}

HRESULT CRepackStreamBase::ProcessEmptyFilesOrDirectory()
{
  while (_currentIndex < _extractStatuses->Size() && _db->Files[_startIndex + _currentIndex].Size == 0)
  {
    RINOK(OpenFile());
    RINOK(CloseFile());
  }
  return S_OK;
}
  


#ifndef _7ZIP_ST

class CFolderOutStream2:
  public CRepackStreamBase,
  public ISequentialOutStream,
  public CMyUnknownImp
{
public:
  CMyComPtr<ISequentialOutStream> _stream;

  MY_UNKNOWN_IMP

  STDMETHOD(Write)(const void *data, UInt32 size, UInt32 *processedSize);
};

STDMETHODIMP CFolderOutStream2::Write(const void *data, UInt32 size, UInt32 *processedSize)
{
  if (processedSize)
    *processedSize = 0;
  
  while (size != 0)
  {
    if (_fileIsOpen)
    {
      UInt32 cur = (size < _rem ? size : (UInt32)_rem);
      HRESULT result = S_OK;
      if (_needWrite)
        result = _stream->Write(data, cur, &cur);
      if (_calcCrc)
        _crc = CrcUpdate(_crc, data, cur);
      if (processedSize)
        *processedSize += cur;
      data = (const Byte *)data + cur;
      size -= cur;
      _rem -= cur;
      if (_rem == 0)
      {
        RINOK(CloseFile());
        RINOK(ProcessEmptyFilesOrDirectory());
      }
      RINOK(result);
      if (cur == 0)
        break;
      continue;
    }

    RINOK(ProcessEmptyFilesOrDirectory());
    if (_currentIndex == _extractStatuses->Size())
    {
      // we don't support write cut here
      return E_FAIL;
    }
    RINOK(OpenFile());
  }

  return S_OK;
}

#endif



static const UInt32 kTempBufSize = 1 << 16;

class CFolderInStream2:
  public CRepackStreamBase,
  public ISequentialInStream,
  public CMyUnknownImp
{
  Byte *_buf;
public:
  CMyComPtr<ISequentialInStream> _inStream;
  HRESULT Result;

  MY_UNKNOWN_IMP

  CFolderInStream2():
      Result(S_OK)
  {
    _buf = new Byte[kTempBufSize];
  }

  ~CFolderInStream2()
  {
    delete []_buf;
  }

  void Init() { Result = S_OK; }
  STDMETHOD(Read)(void *data, UInt32 size, UInt32 *processedSize);
};

STDMETHODIMP CFolderInStream2::Read(void *data, UInt32 size, UInt32 *processedSize)
{
  if (processedSize)
    *processedSize = 0;
  
  while (size != 0)
  {
    if (_fileIsOpen)
    {
      UInt32 cur = (size < _rem ? size : (UInt32)_rem);
      
      void *buf;
      if (_needWrite)
        buf = data;
      else
      {
        buf = _buf;
        if (cur > kTempBufSize)
          cur = kTempBufSize;
      }

      HRESULT result = _inStream->Read(buf, cur, &cur);
      _crc = CrcUpdate(_crc, buf, cur);
      _rem -= cur;

      if (_needWrite)
      {
        data = (Byte *)data + cur;
        size -= cur;
        if (processedSize)
          *processedSize += cur;
      }

      if (result != S_OK)
        Result = result;

      if (_rem == 0)
      {
        RINOK(CloseFile());
        RINOK(ProcessEmptyFilesOrDirectory());
      }

      RINOK(result);
      
      if (cur == 0)
        return E_FAIL;

      continue;
    }

    RINOK(ProcessEmptyFilesOrDirectory());
    if (_currentIndex == _extractStatuses->Size())
    {
      return S_OK;
    }
    RINOK(OpenFile());
  }
  
  return S_OK;
}


class CThreadDecoder
  #ifndef _7ZIP_ST
    : public CVirtThread
  #endif
{
public:
  CDecoder Decoder;

  CThreadDecoder(bool multiThreadMixer):
      Decoder(multiThreadMixer)
  {
    #ifndef _7ZIP_ST
    if (multiThreadMixer)
    {
      MtMode = false;
      NumThreads = 1;
      FosSpec = new CFolderOutStream2;
      Fos = FosSpec;
      Result = E_FAIL;
    }
    #endif
    // UnpackSize = 0;
    // send_UnpackSize = false;
  }

  #ifndef _7ZIP_ST
  
  HRESULT Result;
  CMyComPtr<IInStream> InStream;

  CFolderOutStream2 *FosSpec;
  CMyComPtr<ISequentialOutStream> Fos;

  UInt64 StartPos;
  const CFolders *Folders;
  int FolderIndex;

  // bool send_UnpackSize;
  // UInt64 UnpackSize;
  
  #ifndef _NO_CRYPTO
  CMyComPtr<ICryptoGetTextPassword> getTextPassword;
  #endif

  DECL_EXTERNAL_CODECS_LOC_VARS2;

  #ifndef _7ZIP_ST
  bool MtMode;
  UInt32 NumThreads;
  #endif

  
  ~CThreadDecoder() { CVirtThread::WaitThreadFinish(); }
  virtual void Execute();

  #endif
};

#ifndef _7ZIP_ST

void CThreadDecoder::Execute()
{
  try
  {
    #ifndef _NO_CRYPTO
      bool isEncrypted = false;
      bool passwordIsDefined = false;
      UString password;
    #endif
    
    Result = Decoder.Decode(
      EXTERNAL_CODECS_LOC_VARS
      InStream,
      StartPos,
      *Folders, FolderIndex,
      
      // send_UnpackSize ? &UnpackSize : NULL,
      NULL, // unpackSize : FULL unpack
      
      Fos,
      NULL, // compressProgress
      NULL  // *inStreamMainRes

      _7Z_DECODER_CRYPRO_VARS
      #ifndef _7ZIP_ST
        , MtMode, NumThreads
      #endif
      );
  }
  catch(...)
  {
    Result = E_FAIL;
  }
  
  /*
  if (Result == S_OK)
    Result = FosSpec->CheckFinishedState();
  */
  FosSpec->_stream.Release();
}

#endif

#ifndef _NO_CRYPTO

class CCryptoGetTextPassword:
  public ICryptoGetTextPassword,
  public CMyUnknownImp
{
public:
  UString Password;

  MY_UNKNOWN_IMP
  STDMETHOD(CryptoGetTextPassword)(BSTR *password);
};

STDMETHODIMP CCryptoGetTextPassword::CryptoGetTextPassword(BSTR *password)
{
  return StringToBstr(Password, password);
}

#endif


static void GetFile(const CDatabase &inDb, unsigned index, CFileItem &file, CFileItem2 &file2)
{
  file = inDb.Files[index];
  file2.CTimeDefined = inDb.CTime.GetItem(index, file2.CTime);
  file2.ATimeDefined = inDb.ATime.GetItem(index, file2.ATime);
  file2.MTimeDefined = inDb.MTime.GetItem(index, file2.MTime);
  file2.StartPosDefined = inDb.StartPos.GetItem(index, file2.StartPos);
  file2.IsAnti = inDb.IsItemAnti(index);
  // file2.IsAux = inDb.IsItemAux(index);
}

HRESULT Update(
    DECL_EXTERNAL_CODECS_LOC_VARS
    IInStream *inStream,
    const CDbEx *db,
    const CObjectVector<CUpdateItem> &updateItems,
    // const CObjectVector<CTreeFolder> &treeFolders,
    // const CUniqBlocks &secureBlocks,
    COutArchive &archive,
    CArchiveDatabaseOut &newDatabase,
    ISequentialOutStream *seqOutStream,
    IArchiveUpdateCallback *updateCallback,
    const CUpdateOptions &options
    #ifndef _NO_CRYPTO
    , ICryptoGetTextPassword *getDecoderPassword
    #endif
    )
{
  UInt64 numSolidFiles = options.NumSolidFiles;
  if (numSolidFiles == 0)
    numSolidFiles = 1;

  CMyComPtr<IArchiveUpdateCallbackFile> opCallback;
  updateCallback->QueryInterface(IID_IArchiveUpdateCallbackFile, (void **)&opCallback);

  CMyComPtr<IArchiveExtractCallbackMessage> extractCallback;
  updateCallback->QueryInterface(IID_IArchiveExtractCallbackMessage, (void **)&extractCallback);

  // size_t totalSecureDataSize = (size_t)secureBlocks.GetTotalSizeInBytes();

  /*
  CMyComPtr<IOutStream> outStream;
  RINOK(seqOutStream->QueryInterface(IID_IOutStream, (void **)&outStream));
  if (!outStream)
    return E_NOTIMPL;
  */

  UInt64 startBlockSize = db ? db->ArcInfo.StartPosition: 0;
  if (startBlockSize > 0 && !options.RemoveSfxBlock)
  {
    RINOK(WriteRange(inStream, seqOutStream, 0, startBlockSize, NULL));
  }

  CIntArr fileIndexToUpdateIndexMap;
  UInt64 complexity = 0;
  UInt64 inSizeForReduce2 = 0;
  bool needEncryptedRepack = false;

  CRecordVector<CFilterMode2> filters;
  CObjectVector<CSolidGroup> groups;
  bool thereAreRepacks = false;

  bool useFilters = options.UseFilters;
  if (useFilters)
  {
    const CCompressionMethodMode &method = *options.Method;

    FOR_VECTOR (i, method.Methods)
      if (IsFilterMethod(method.Methods[i].Id))
      {
        useFilters = false;
        break;
      }
  }
  
  if (db)
  {
    fileIndexToUpdateIndexMap.Alloc(db->Files.Size());
    unsigned i;
    
    for (i = 0; i < db->Files.Size(); i++)
      fileIndexToUpdateIndexMap[i] = -1;

    for (i = 0; i < updateItems.Size(); i++)
    {
      int index = updateItems[i].IndexInArchive;
      if (index != -1)
        fileIndexToUpdateIndexMap[(unsigned)index] = i;
    }

    for (i = 0; i < db->NumFolders; i++)
    {
      CNum indexInFolder = 0;
      CNum numCopyItems = 0;
      CNum numUnpackStreams = db->NumUnpackStreamsVector[i];
      UInt64 repackSize = 0;
      
      for (CNum fi = db->FolderStartFileIndex[i]; indexInFolder < numUnpackStreams; fi++)
      {
        const CFileItem &file = db->Files[fi];
        if (file.HasStream)
        {
          indexInFolder++;
          int updateIndex = fileIndexToUpdateIndexMap[fi];
          if (updateIndex >= 0 && !updateItems[updateIndex].NewData)
          {
            numCopyItems++;
            repackSize += file.Size;
          }
        }
      }

      if (numCopyItems == 0)
        continue;

      CFolderRepack rep;
      rep.FolderIndex = i;
      rep.NumCopyFiles = numCopyItems;
      CFolderEx f;
      db->ParseFolderEx(i, f);

      const bool isEncrypted = f.IsEncrypted();
      const bool needCopy = (numCopyItems == numUnpackStreams);
      const bool extractFilter = (useFilters || needCopy);

      unsigned groupIndex = Get_FilterGroup_for_Folder(filters, f, extractFilter);
      
      while (groupIndex >= groups.Size())
        groups.AddNew();

      groups[groupIndex].folderRefs.Add(rep);
      
      if (needCopy)
        complexity += db->GetFolderFullPackSize(i);
      else
      {
        thereAreRepacks = true;
        complexity += repackSize;
        if (inSizeForReduce2 < repackSize)
          inSizeForReduce2 = repackSize;
        if (isEncrypted)
          needEncryptedRepack = true;
      }
    }
  }

  UInt64 inSizeForReduce = 0;
  {
    FOR_VECTOR (i, updateItems)
    {
      const CUpdateItem &ui = updateItems[i];
      if (ui.NewData)
      {
        complexity += ui.Size;
        if (numSolidFiles != 1)
          inSizeForReduce += ui.Size;
        else if (inSizeForReduce < ui.Size)
          inSizeForReduce = ui.Size;
      }
    }
  }

  if (inSizeForReduce < inSizeForReduce2)
    inSizeForReduce = inSizeForReduce2;

  RINOK(updateCallback->SetTotal(complexity));

  CLocalProgress *lps = new CLocalProgress;
  CMyComPtr<ICompressProgressInfo> progress = lps;
  lps->Init(updateCallback, true);

  #ifndef _7ZIP_ST
  
  CStreamBinder sb;
  if (options.MultiThreadMixer)
  {
    RINOK(sb.CreateEvents());
  }
  
  #endif

  CThreadDecoder threadDecoder(options.MultiThreadMixer);
  
  #ifndef _7ZIP_ST
  if (options.MultiThreadMixer && thereAreRepacks)
  {
    #ifdef EXTERNAL_CODECS
    threadDecoder.__externalCodecs = __externalCodecs;
    #endif
    RINOK(threadDecoder.Create());
  }
  #endif

  {
    CAnalysis analysis;
    if (options.AnalysisLevel == 0)
    {
      analysis.ParseWav = false;
      analysis.ParseExe = false;
      analysis.ParseAll = false;
    }
    else
    {
      analysis.Callback = opCallback;
      if (options.AnalysisLevel > 0)
      {
        analysis.ParseWav = true;
        if (options.AnalysisLevel >= 7)
        {
          analysis.ParseExe = true;
          if (options.AnalysisLevel >= 9)
            analysis.ParseAll = true;
        }
      }
    }

    // ---------- Split files to groups ----------

    const CCompressionMethodMode &method = *options.Method;
    
    FOR_VECTOR (i, updateItems)
    {
      const CUpdateItem &ui = updateItems[i];
      if (!ui.NewData || !ui.HasStream())
        continue;

      CFilterMode2 fm;
      if (useFilters)
      {
        RINOK(analysis.GetFilterGroup(i, ui, fm));
      }
      fm.Encrypted = method.PasswordIsDefined;

      unsigned groupIndex = GetGroup(filters, fm);
      while (groupIndex >= groups.Size())
        groups.AddNew();
      groups[groupIndex].Indices.Add(i);
    }
  }


  #ifndef _NO_CRYPTO

  CCryptoGetTextPassword *getPasswordSpec = NULL;
  CMyComPtr<ICryptoGetTextPassword> getTextPassword;
  if (needEncryptedRepack)
  {
    getPasswordSpec = new CCryptoGetTextPassword;
    getTextPassword = getPasswordSpec;
    
    #ifndef _7ZIP_ST
    threadDecoder.getTextPassword = getPasswordSpec;
    #endif

    if (options.Method->PasswordIsDefined)
      getPasswordSpec->Password = options.Method->Password;
    else
    {
      if (!getDecoderPassword)
        return E_NOTIMPL;
      CMyComBSTR password;
      RINOK(getDecoderPassword->CryptoGetTextPassword(&password));
      if (password)
        getPasswordSpec->Password = password;
    }
  }

  #endif

  
  // ---------- Compress ----------

  RINOK(archive.Create(seqOutStream, false));
  RINOK(archive.SkipPrefixArchiveHeader());

  /*
  CIntVector treeFolderToArcIndex;
  treeFolderToArcIndex.Reserve(treeFolders.Size());
  for (i = 0; i < treeFolders.Size(); i++)
    treeFolderToArcIndex.Add(-1);
  // ---------- Write Tree (only AUX dirs) ----------
  for (i = 1; i < treeFolders.Size(); i++)
  {
    const CTreeFolder &treeFolder = treeFolders[i];
    CFileItem file;
    CFileItem2 file2;
    file2.Init();
    int secureID = 0;
    if (treeFolder.UpdateItemIndex < 0)
    {
      // we can store virtual dir item wuthout attrib, but we want all items have attrib.
      file.SetAttrib(FILE_ATTRIBUTE_DIRECTORY);
      file2.IsAux = true;
    }
    else
    {
      const CUpdateItem &ui = updateItems[treeFolder.UpdateItemIndex];
      // if item is not dir, then it's parent for alt streams.
      // we will write such items later
      if (!ui.IsDir)
        continue;
      secureID = ui.SecureIndex;
      if (ui.NewProps)
        FromUpdateItemToFileItem(ui, file, file2);
      else
        GetFile(*db, ui.IndexInArchive, file, file2);
    }
    file.Size = 0;
    file.HasStream = false;
    file.IsDir = true;
    file.Parent = treeFolder.Parent;
    
    treeFolderToArcIndex[i] = newDatabase.Files.Size();
    newDatabase.AddFile(file, file2, treeFolder.Name);
    
    if (totalSecureDataSize != 0)
      newDatabase.SecureIDs.Add(secureID);
  }
  */

  {
    /* ---------- Write non-AUX dirs and Empty files ---------- */
    CUIntVector emptyRefs;
    
    unsigned i;

    for (i = 0; i < updateItems.Size(); i++)
    {
      const CUpdateItem &ui = updateItems[i];
      if (ui.NewData)
      {
        if (ui.HasStream())
          continue;
      }
      else if (ui.IndexInArchive != -1 && db->Files[ui.IndexInArchive].HasStream)
        continue;
      /*
      if (ui.TreeFolderIndex >= 0)
        continue;
      */
      emptyRefs.Add(i);
    }
    
    emptyRefs.Sort(CompareEmptyItems, (void *)&updateItems);
    
    for (i = 0; i < emptyRefs.Size(); i++)
    {
      const CUpdateItem &ui = updateItems[emptyRefs[i]];
      CFileItem file;
      CFileItem2 file2;
      UString name;
      if (ui.NewProps)
      {
        FromUpdateItemToFileItem(ui, file, file2);
        name = ui.Name;
      }
      else
      {
        GetFile(*db, ui.IndexInArchive, file, file2);
        db->GetPath(ui.IndexInArchive, name);
      }
      
      /*
      if (totalSecureDataSize != 0)
        newDatabase.SecureIDs.Add(ui.SecureIndex);
      file.Parent = ui.ParentFolderIndex;
      */
      newDatabase.AddFile(file, file2, name);
    }
  }

  lps->ProgressOffset = 0;

  {
    // ---------- Sort Filters ----------
    
    FOR_VECTOR (i, filters)
    {
      filters[i].GroupIndex = i;
    }
    filters.Sort2();
  }

  for (unsigned groupIndex = 0; groupIndex < filters.Size(); groupIndex++)
  {
    const CFilterMode2 &filterMode = filters[groupIndex];

    CCompressionMethodMode method = *options.Method;
    {
      HRESULT res = MakeExeMethod(method, filterMode,
        #ifdef _7ZIP_ST
          false
        #else
          options.MaxFilter && options.MultiThreadMixer
        #endif
        );

      RINOK(res);
    }

    if (filterMode.Encrypted)
    {
      if (!method.PasswordIsDefined)
      {
        #ifndef _NO_CRYPTO
        if (getPasswordSpec)
          method.Password = getPasswordSpec->Password;
        #endif
        method.PasswordIsDefined = true;
      }
    }
    else
    {
      method.PasswordIsDefined = false;
      method.Password.Empty();
    }

    CEncoder encoder(method);

    // ---------- Repack and copy old solid blocks ----------

    const CSolidGroup &group = groups[filterMode.GroupIndex];
    
    FOR_VECTOR(folderRefIndex, group.folderRefs)
    {
      const CFolderRepack &rep = group.folderRefs[folderRefIndex];

      unsigned folderIndex = rep.FolderIndex;
      
      CNum numUnpackStreams = db->NumUnpackStreamsVector[folderIndex];

      if (rep.NumCopyFiles == numUnpackStreams)
      {
        if (opCallback)
        {
          RINOK(opCallback->ReportOperation(
              NEventIndexType::kBlockIndex, (UInt32)folderIndex,
              NUpdateNotifyOp::kReplicate));

          // ---------- Copy old solid block ----------
          {
            CNum indexInFolder = 0;
            for (CNum fi = db->FolderStartFileIndex[folderIndex]; indexInFolder < numUnpackStreams; fi++)
            {
              if (db->Files[fi].HasStream)
              {
                indexInFolder++;
                RINOK(opCallback->ReportOperation(
                    NEventIndexType::kInArcIndex, (UInt32)fi,
                    NUpdateNotifyOp::kReplicate));
              }
            }
          }
        }

        UInt64 packSize = db->GetFolderFullPackSize(folderIndex);
        RINOK(WriteRange(inStream, archive.SeqStream,
            db->GetFolderStreamPos(folderIndex, 0), packSize, progress));
        lps->ProgressOffset += packSize;
        
        CFolder &folder = newDatabase.Folders.AddNew();
        db->ParseFolderInfo(folderIndex, folder);
        CNum startIndex = db->FoStartPackStreamIndex[folderIndex];
        FOR_VECTOR(j, folder.PackStreams)
        {
          newDatabase.PackSizes.Add(db->GetStreamPackSize(startIndex + j));
          // newDatabase.PackCRCsDefined.Add(db.PackCRCsDefined[startIndex + j]);
          // newDatabase.PackCRCs.Add(db.PackCRCs[startIndex + j]);
        }

        size_t indexStart = db->FoToCoderUnpackSizes[folderIndex];
        size_t indexEnd = db->FoToCoderUnpackSizes[folderIndex + 1];
        for (; indexStart < indexEnd; indexStart++)
          newDatabase.CoderUnpackSizes.Add(db->CoderUnpackSizes[indexStart]);
      }
      else
      {
        // ---------- Repack old solid block ----------

        CBoolVector extractStatuses;
        
        CNum indexInFolder = 0;

        if (opCallback)
        {
          RINOK(opCallback->ReportOperation(
              NEventIndexType::kBlockIndex, (UInt32)folderIndex,
              NUpdateNotifyOp::kRepack))
        }

        /* We could reduce data size of decoded folder, if we don't need to repack
           last files in folder. But the gain in speed is small in most cases.
           So we unpack full folder. */
           
        UInt64 sizeToEncode = 0;
  
        /*
        UInt64 importantUnpackSize = 0;
        unsigned numImportantFiles = 0;
        UInt64 decodeSize = 0;
        */

        for (CNum fi = db->FolderStartFileIndex[folderIndex]; indexInFolder < numUnpackStreams; fi++)
        {
          bool needExtract = false;
          const CFileItem &file = db->Files[fi];
  
          if (file.HasStream)
          {
            indexInFolder++;
            int updateIndex = fileIndexToUpdateIndexMap[fi];
            if (updateIndex >= 0 && !updateItems[updateIndex].NewData)
              needExtract = true;
            // decodeSize += file.Size;
          }
          
          extractStatuses.Add(needExtract);
          if (needExtract)
          {
            sizeToEncode += file.Size;
            /*
            numImportantFiles = extractStatuses.Size();
            importantUnpackSize = decodeSize;
            */
          }
        }

        // extractStatuses.DeleteFrom(numImportantFiles);

        unsigned startPackIndex = newDatabase.PackSizes.Size();
        UInt64 curUnpackSize;
        {

          CMyComPtr<ISequentialInStream> sbInStream;
          CRepackStreamBase *repackBase;
          CFolderInStream2 *FosSpec2 = NULL;

          CRepackInStreamWithSizes *inStreamSizeCountSpec = new CRepackInStreamWithSizes;
          CMyComPtr<ISequentialInStream> inStreamSizeCount = inStreamSizeCountSpec;
          {
            #ifndef _7ZIP_ST
            if (options.MultiThreadMixer)
            {
              repackBase = threadDecoder.FosSpec;
              CMyComPtr<ISequentialOutStream> sbOutStream;
              sb.CreateStreams(&sbInStream, &sbOutStream);
              sb.ReInit();
              
              threadDecoder.FosSpec->_stream = sbOutStream;
              
              threadDecoder.InStream = inStream;
              threadDecoder.StartPos = db->ArcInfo.DataStartPosition; // db->GetFolderStreamPos(folderIndex, 0);
              threadDecoder.Folders = (const CFolders *)db;
              threadDecoder.FolderIndex = folderIndex;
             
              // threadDecoder.UnpackSize = importantUnpackSize;
              // threadDecoder.send_UnpackSize = true;
            }
            else
            #endif
            {
              FosSpec2 = new CFolderInStream2;
              FosSpec2->Init();
              sbInStream = FosSpec2;
              repackBase = FosSpec2;

              #ifndef _NO_CRYPTO
              bool isEncrypted = false;
              bool passwordIsDefined = false;
              UString password;
              #endif
              
              CMyComPtr<ISequentialInStream> decodedStream;
              HRESULT res = threadDecoder.Decoder.Decode(
                  EXTERNAL_CODECS_LOC_VARS
                  inStream,
                  db->ArcInfo.DataStartPosition, // db->GetFolderStreamPos(folderIndex, 0);,
                  *db, folderIndex,
                  // &importantUnpackSize, // *unpackSize
                  NULL, // *unpackSize : FULL unpack
                
                  NULL, // *outStream
                  NULL, // *compressProgress
                  &decodedStream
                
                  _7Z_DECODER_CRYPRO_VARS
                  #ifndef _7ZIP_ST
                    , false // mtMode
                    , 1 // numThreads
                  #endif
                );
          
              RINOK(res);
              if (!decodedStream)
                return E_FAIL;

              FosSpec2->_inStream = decodedStream;
            }

            repackBase->_db = db;
            repackBase->_opCallback = opCallback;
            repackBase->_extractCallback = extractCallback;

            UInt32 startIndex = db->FolderStartFileIndex[folderIndex];
            RINOK(repackBase->Init(startIndex, &extractStatuses));

            inStreamSizeCountSpec->_db = db;
            inStreamSizeCountSpec->Init(sbInStream, startIndex, &extractStatuses);

            #ifndef _7ZIP_ST
            if (options.MultiThreadMixer)
            {
              threadDecoder.Start();
            }
            #endif
          }


          HRESULT encodeRes = encoder.Encode(
              EXTERNAL_CODECS_LOC_VARS
              inStreamSizeCount,
              // NULL,
              &inSizeForReduce,
              newDatabase.Folders.AddNew(), newDatabase.CoderUnpackSizes, curUnpackSize,
              archive.SeqStream, newDatabase.PackSizes, progress);

          if (encodeRes == k_My_HRESULT_CRC_ERROR)
            return E_FAIL;

          #ifndef _7ZIP_ST
          if (options.MultiThreadMixer)
          {
            // 16.00: hang was fixed : for case if decoding was not finished.
            // We close CBinderInStream and it calls CStreamBinder::CloseRead()
            inStreamSizeCount.Release();
            sbInStream.Release();
            
            threadDecoder.WaitExecuteFinish();
            
            HRESULT decodeRes = threadDecoder.Result;
            // if (res == k_My_HRESULT_CRC_ERROR)
            if (decodeRes == S_FALSE)
            {
              if (extractCallback)
              {
                RINOK(extractCallback->ReportExtractResult(
                    NEventIndexType::kInArcIndex, db->FolderStartFileIndex[folderIndex],
                    // NEventIndexType::kBlockIndex, (UInt32)folderIndex,
                    NExtract::NOperationResult::kDataError));
              }
              return E_FAIL;
            }
            RINOK(decodeRes);
            if (encodeRes == S_OK)
              if (sb.ProcessedSize != sizeToEncode)
                encodeRes = E_FAIL;
          }
          else
          #endif
          {
            if (FosSpec2->Result == S_FALSE)
            {
              if (extractCallback)
              {
                RINOK(extractCallback->ReportExtractResult(
                    NEventIndexType::kBlockIndex, (UInt32)folderIndex,
                    NExtract::NOperationResult::kDataError));
              }
              return E_FAIL;
            }
            RINOK(FosSpec2->Result);
          }

          RINOK(encodeRes);
          RINOK(repackBase->CheckFinishedState());

          if (curUnpackSize != sizeToEncode)
            return E_FAIL;
        }

        for (; startPackIndex < newDatabase.PackSizes.Size(); startPackIndex++)
          lps->OutSize += newDatabase.PackSizes[startPackIndex];
        lps->InSize += curUnpackSize;
      }
      
      newDatabase.NumUnpackStreamsVector.Add(rep.NumCopyFiles);
      
      CNum indexInFolder = 0;
      for (CNum fi = db->FolderStartFileIndex[folderIndex]; indexInFolder < numUnpackStreams; fi++)
      {
        CFileItem file;
        CFileItem2 file2;
        GetFile(*db, fi, file, file2);
        UString name;
        db->GetPath(fi, name);
        if (file.HasStream)
        {
          indexInFolder++;
          int updateIndex = fileIndexToUpdateIndexMap[fi];
          if (updateIndex >= 0)
          {
            const CUpdateItem &ui = updateItems[updateIndex];
            if (ui.NewData)
              continue;
            if (ui.NewProps)
            {
              CFileItem uf;
              FromUpdateItemToFileItem(ui, uf, file2);
              uf.Size = file.Size;
              uf.Crc = file.Crc;
              uf.CrcDefined = file.CrcDefined;
              uf.HasStream = file.HasStream;
              file = uf;
              name = ui.Name;
            }
            /*
            file.Parent = ui.ParentFolderIndex;
            if (ui.TreeFolderIndex >= 0)
              treeFolderToArcIndex[ui.TreeFolderIndex] = newDatabase.Files.Size();
            if (totalSecureDataSize != 0)
              newDatabase.SecureIDs.Add(ui.SecureIndex);
            */
            newDatabase.AddFile(file, file2, name);
          }
        }
      }
    }


    // ---------- Compress files to new solid blocks ----------

    unsigned numFiles = group.Indices.Size();
    if (numFiles == 0)
      continue;
    CRecordVector<CRefItem> refItems;
    refItems.ClearAndSetSize(numFiles);
    bool sortByType = (options.UseTypeSorting && numSolidFiles > 1);
    
    unsigned i;

    for (i = 0; i < numFiles; i++)
      refItems[i] = CRefItem(group.Indices[i], updateItems[group.Indices[i]], sortByType);

    CSortParam sortParam;
    // sortParam.TreeFolders = &treeFolders;
    sortParam.SortByType = sortByType;
    refItems.Sort(CompareUpdateItems, (void *)&sortParam);
    
    CObjArray<UInt32> indices(numFiles);

    for (i = 0; i < numFiles; i++)
    {
      UInt32 index = refItems[i].Index;
      indices[i] = index;
      /*
      const CUpdateItem &ui = updateItems[index];
      CFileItem file;
      if (ui.NewProps)
        FromUpdateItemToFileItem(ui, file);
      else
        file = db.Files[ui.IndexInArchive];
      if (file.IsAnti || file.IsDir)
        return E_FAIL;
      newDatabase.Files.Add(file);
      */
    }
    
    for (i = 0; i < numFiles;)
    {
      UInt64 totalSize = 0;
      unsigned numSubFiles;
      
      const wchar_t *prevExtension = NULL;
      
      for (numSubFiles = 0; i + numSubFiles < numFiles && numSubFiles < numSolidFiles; numSubFiles++)
      {
        const CUpdateItem &ui = updateItems[indices[i + numSubFiles]];
        totalSize += ui.Size;
        if (totalSize > options.NumSolidBytes)
          break;
        if (options.SolidExtension)
        {
          int slashPos = ui.Name.ReverseFind_PathSepar();
          int dotPos = ui.Name.ReverseFind_Dot();
          const wchar_t *ext = ui.Name.Ptr(dotPos <= slashPos ? ui.Name.Len() : dotPos + 1);
          if (numSubFiles == 0)
            prevExtension = ext;
          else if (!StringsAreEqualNoCase(ext, prevExtension))
            break;
        }
      }

      if (numSubFiles < 1)
        numSubFiles = 1;

      RINOK(lps->SetCur());

      CFolderInStream *inStreamSpec = new CFolderInStream;
      CMyComPtr<ISequentialInStream> solidInStream(inStreamSpec);
      inStreamSpec->Init(updateCallback, &indices[i], numSubFiles);
      
      unsigned startPackIndex = newDatabase.PackSizes.Size();
      UInt64 curFolderUnpackSize;
      RINOK(encoder.Encode(
          EXTERNAL_CODECS_LOC_VARS
          solidInStream,
          // NULL,
          &inSizeForReduce,
          newDatabase.Folders.AddNew(), newDatabase.CoderUnpackSizes, curFolderUnpackSize,
          archive.SeqStream, newDatabase.PackSizes, progress));

      if (!inStreamSpec->WasFinished())
        return E_FAIL;

      for (; startPackIndex < newDatabase.PackSizes.Size(); startPackIndex++)
        lps->OutSize += newDatabase.PackSizes[startPackIndex];

      lps->InSize += curFolderUnpackSize;
      // for ()
      // newDatabase.PackCRCsDefined.Add(false);
      // newDatabase.PackCRCs.Add(0);

      CNum numUnpackStreams = 0;
      UInt64 skippedSize = 0;
      
      for (unsigned subIndex = 0; subIndex < numSubFiles; subIndex++)
      {
        const CUpdateItem &ui = updateItems[indices[i + subIndex]];
        CFileItem file;
        CFileItem2 file2;
        UString name;
        if (ui.NewProps)
        {
          FromUpdateItemToFileItem(ui, file, file2);
          name = ui.Name;
        }
        else
        {
          GetFile(*db, ui.IndexInArchive, file, file2);
          db->GetPath(ui.IndexInArchive, name);
        }
        if (file2.IsAnti || file.IsDir)
          return E_FAIL;
        
        /*
        CFileItem &file = newDatabase.Files[
              startFileIndexInDatabase + i + subIndex];
        */
        if (!inStreamSpec->Processed[subIndex])
        {
          skippedSize += ui.Size;
          continue;
          // file.Name.AddAscii(".locked");
        }

        file.Crc = inStreamSpec->CRCs[subIndex];
        file.Size = inStreamSpec->Sizes[subIndex];
        
        // if (file.Size >= 0) // test purposes
        if (file.Size != 0)
        {
          file.CrcDefined = true;
          file.HasStream = true;
          numUnpackStreams++;
        }
        else
        {
          file.CrcDefined = false;
          file.HasStream = false;
        }

        /*
        file.Parent = ui.ParentFolderIndex;
        if (ui.TreeFolderIndex >= 0)
          treeFolderToArcIndex[ui.TreeFolderIndex] = newDatabase.Files.Size();
        if (totalSecureDataSize != 0)
          newDatabase.SecureIDs.Add(ui.SecureIndex);
        */
        newDatabase.AddFile(file, file2, name);
      }

      // numUnpackStreams = 0 is very bad case for locked files
      // v3.13 doesn't understand it.
      newDatabase.NumUnpackStreamsVector.Add(numUnpackStreams);
      i += numSubFiles;

      if (skippedSize != 0 && complexity >= skippedSize)
      {
        complexity -= skippedSize;
        RINOK(updateCallback->SetTotal(complexity));
      }
    }
  }

  RINOK(lps->SetCur());

  /*
  fileIndexToUpdateIndexMap.ClearAndFree();
  groups.ClearAndFree();
  */

  /*
  for (i = 0; i < newDatabase.Files.Size(); i++)
  {
    CFileItem &file = newDatabase.Files[i];
    file.Parent = treeFolderToArcIndex[file.Parent];
  }

  if (totalSecureDataSize != 0)
  {
    newDatabase.SecureBuf.SetCapacity(totalSecureDataSize);
    size_t pos = 0;
    newDatabase.SecureSizes.Reserve(secureBlocks.Sorted.Size());
    for (i = 0; i < secureBlocks.Sorted.Size(); i++)
    {
      const CByteBuffer &buf = secureBlocks.Bufs[secureBlocks.Sorted[i]];
      size_t size = buf.GetCapacity();
      if (size != 0)
        memcpy(newDatabase.SecureBuf + pos, buf, size);
      newDatabase.SecureSizes.Add((UInt32)size);
      pos += size;
    }
  }
  */
  newDatabase.ReserveDown();

  if (opCallback)
    RINOK(opCallback->ReportOperation(NEventIndexType::kNoIndex, (UInt32)(Int32)-1, NUpdateNotifyOp::kHeader));

  return S_OK;
}

}}
