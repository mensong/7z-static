#include "ArchiveExtractCallbackMemory.h"
#include "PropVariant2.h"
#include "FileSys.h"
#include "OutStreamWrapperMemory.h"
#include <comdef.h>
#include "../CPP/Common/MyCom.h"
#include "FileStreamMemory.h"
#include "GetArchiveItemInfoHelper.h"

namespace SevenZip
{
	namespace intl
	{

		const TString EmptyFileAlias = _T("[Content]");


		ArchiveExtractCallbackMemory::ArchiveExtractCallbackMemory(
			const CMyComPtr< IInArchive >& archiveHandler,
			ProgressCallback* callback,
			CFileStream &fileStreams)
			: m_refCount(0)
			, m_archiveHandler(archiveHandler)
			, m_callback(callback)
			, m_totalSize(0)
			, PasswordIsDefined(false)
			, _fileStreams(fileStreams)
            , m_index(-1)
		{ 

		}

		ArchiveExtractCallbackMemory::~ArchiveExtractCallbackMemory()
		{
		}

		STDMETHODIMP ArchiveExtractCallbackMemory::QueryInterface(REFIID iid, void** ppvObject)
		{
			if (iid == __uuidof(IUnknown))
			{
				*ppvObject = reinterpret_cast<IUnknown*>(this);
				AddRef();
				return S_OK;
			}

			if (iid == IID_IArchiveExtractCallback)
			{
				*ppvObject = static_cast<IArchiveExtractCallback*>(this);
				AddRef();
				return S_OK;
			}

			if (iid == IID_ICryptoGetTextPassword)
			{
				*ppvObject = static_cast<ICryptoGetTextPassword*>(this);
				AddRef();
				return S_OK;
			}

			if (iid == IID_ICompressProgressInfo)
			{
				*ppvObject = static_cast<ICompressProgressInfo*>(this);
				AddRef();
				return S_OK;
			}

			return E_NOINTERFACE;
		}

		STDMETHODIMP_(ULONG) ArchiveExtractCallbackMemory::AddRef()
		{
			return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
		}

		STDMETHODIMP_(ULONG) ArchiveExtractCallbackMemory::Release()
		{
			ULONG res = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
			if (res == 0)
			{
				delete this;
			}
			return res;
		}

		STDMETHODIMP ArchiveExtractCallbackMemory::SetTotal(UInt64 size)
		{
//#ifdef _DEBUG
//			wprintf_s(L"SetTotal:%llu\n", size);
//#endif // _DEBUG

			m_totalSize = size;
			//	- SetTotal is never called for ZIP and 7z formats
			if (m_callback)
			{
				m_callback->OnWorkStart(L":memory:", size);
			}
			return S_OK;
		}

		STDMETHODIMP ArchiveExtractCallbackMemory::SetCompleted(const UInt64* completeValue)
		{
//#ifdef _DEBUG
//			wprintf_s(L"SetCompleted:%llu\n", *completeValue);
//#endif // _DEBUG

			//Callback Event calls
			/*
			NB:
			- For ZIP format SetCompleted only called once per 1000 files in central directory and once per 100 in local ones.
			- For 7Z format SetCompleted is never called.
			*/
			if (m_callback)
			{
				//Don't call this directly, it will be called per file which is more consistent across archive types
				//TODO: incorporate better progress tracking
				//m_callback->OnWorkProgress(m_absPath, *completeValue);
			}
			return S_OK;
		}

		STDMETHODIMP ArchiveExtractCallbackMemory::SetRatioInfo(const UInt64 *inSize, const UInt64 *outSize)
		{
//#ifdef _DEBUG
//			wprintf_s(L"SetRatioInfo:%llu-%llu\n", *inSize, *outSize);
//#endif // _DEBUG

			if (m_callback)
				m_callback->OnWorkRadio(L":memory:", *inSize, *outSize);
			return S_OK;
		}


		STDMETHODIMP ArchiveExtractCallbackMemory::GetStream(UInt32 index, ISequentialOutStream** outStream, Int32 askExtractMode)
		{
			try
			{
                // Retrieve all the various properties for the file at this index.

                //获得压缩包中节点的相对文件名/文件夹名                
                GetPropertyFilePath(index);
                //获得压缩包中的文件/文件夹的属性
                GetPropertyAttributes(index);
                //获得压缩包中节点是否是目录节点
                GetPropertyIsDir(index);
                //获得压缩包中节点的修改时间
                GetPropertyModifiedTime(index);
                //获得压缩包中节点的创建时间
                GetPropertyCreatedTime(index);
                //获得压缩包中节点的最后访问时间
                GetPropertyAccessedTime(index);
                //获得压缩包中节点的大小
                GetPropertySize(index);
			}
			catch (_com_error& ex)
			{
				wprintf_s(L"获取数据失败\n");
				return ex.Error();
			}

			// TODO: m_directory could be a relative path as "..\"
			//m_absPath = m_relPath;
            m_index = index;

            if (m_callback)
            {
                m_callback->OnWorkItemInfo(*((FilePathInfo*)this));
            }

            if (askExtractMode != NArchive::NExtract::NAskMode::kExtract)
                return S_OK;

			if (IsDirectory)
			{
				*outStream = NULL;
				return S_OK;
			}

			if (m_callback)
			{
				if (!m_callback->OnFileExtractBegin(L"", FilePath))
				{
					//stop decompress
					return E_FAIL;
				}
				//Skip file
				if (FilePath.empty())
				{
					*outStream = NULL;
					return S_OK;
				}
			}
			 
			CMyComPtr< IStream > fileStream;
			if (S_OK != FileStreamMemory::OpenFile(FilePath, _fileStreams, &fileStream) || fileStream == NULL)
            {
                wprintf_s(L"创建文件失败:%s\n", FilePath.c_str());
                FilePath.clear();
                return HRESULT_FROM_WIN32(GetLastError());
            }

            OutStreamWrapperMemory * stream = new OutStreamWrapperMemory(fileStream);
            if (!stream)
            {
                wprintf_s(L"内存不足\n");
                return E_OUTOFMEMORY;
            }

			CMyComPtr< OutStreamWrapperMemory > wrapperStream = stream;
            *outStream = wrapperStream.Detach();

            return S_OK;
        }

        STDMETHODIMP ArchiveExtractCallbackMemory::PrepareOperation(Int32 /*askExtractMode*/)
        {
            return S_OK;
        }

        STDMETHODIMP ArchiveExtractCallbackMemory::SetOperationResult(Int32 operationResult)
        {
            if (operationResult != S_OK)
            {
                //_ASSERT_EXPR(FALSE,L"begin rollback");
                bool succ = ProcessRollBack(); succ;
                //_ASSERT_EXPR(succ, L"rollback error!");
                return E_FAIL;
            }
            

            if (FilePath.empty())
            {
                if (m_callback)
                    m_callback->OnWorkEnd(L"");
                return S_OK;
            }
			  

            if (m_callback)
            {
                if(!m_callback->OnFileExtractDone(FilePath, Size))
                    return E_FAIL;
                m_callback->OnWorkProgress(FilePath, Size);
            }
            return S_OK;
        }

        STDMETHODIMP ArchiveExtractCallbackMemory::CryptoGetTextPassword(BSTR* password)
        {
			if (!PasswordIsDefined)
			{
				// You can ask real password here from user
				// Password = GetPassword(OutStream);
				// PasswordIsDefined = true;
				printf("Password is not defined");
				return E_ABORT;
			}
			return StringToBstr(Password, password);
        }

        void ArchiveExtractCallbackMemory::GetPropertyFilePath(UInt32 index)
        {
#if 0
            CPropVariant prop;
            HRESULT hr = m_archiveHandler->GetProperty(index, kpidPath, &prop);
            if (hr != S_OK)
            {
                _com_issue_error(hr);
            }

            if ( prop.vt == VT_EMPTY )
            {
                FilePath = EmptyFileAlias;
            }
            else if (prop.vt != VT_BSTR)
            {
                _com_issue_error(E_FAIL);
            }
            else
            {
                _bstr_t bstr = prop.bstrVal;
#ifdef _UNICODE
                FilePath = bstr;
#else
                char relPath[MAX_PATH];
                int size = WideCharToMultiByte(CP_UTF8, 0, bstr, bstr.length(), relPath, MAX_PATH, NULL, NULL);
                FilePath.assign(relPath, size);
#endif
            }
#else

            if (!GetArchiveItemInfoHelper::GetPropertyFilePath(m_archiveHandler, index, FilePath))
                _com_issue_error(E_FAIL);

#endif
        }

        void ArchiveExtractCallbackMemory::GetPropertyAttributes(UInt32 index)
        {
#if 0
            CPropVariant prop;
            HRESULT hr = m_archiveHandler->GetProperty(index, kpidAttrib, &prop);
            if (hr != S_OK)
            {
                _com_issue_error(hr);
            }

            if (prop.vt == VT_EMPTY)
            {
                Attributes = 0;
                m_hasAttrib = false;
            }
            else if (prop.vt != VT_UI4)
            {
                _com_issue_error(E_FAIL);
            }
            else
            {
                Attributes = prop.ulVal;
                m_hasAttrib = true;
            }
#else

            int res = GetArchiveItemInfoHelper::GetPropertyAttributes(m_archiveHandler, index, Attributes);
            if (res < 0)
                _com_issue_error(E_FAIL);
            else
            {
                m_hasAttrib = (res == 0);
            }

#endif
        }

        void ArchiveExtractCallbackMemory::GetPropertyIsDir(UInt32 index)
        {
#if 0
            CPropVariant prop;
            HRESULT hr = m_archiveHandler->GetProperty(index, kpidIsDir, &prop);
            if (hr != S_OK)
            {
                _com_issue_error(hr);
            }

            if (prop.vt == VT_EMPTY)
            {
                IsDirectory = false;
            }
            else if (prop.vt != VT_BOOL)
            {
                _com_issue_error(E_FAIL);
            }
            else
            {
                IsDirectory = prop.boolVal != VARIANT_FALSE;
            }
#else

            int res = GetArchiveItemInfoHelper::GetPropertyIsDir(m_archiveHandler, index, IsDirectory);
            if (res < 0)
                _com_issue_error(E_FAIL);

#endif
        }

        void ArchiveExtractCallbackMemory::GetPropertyModifiedTime(UInt32 index)
        {
#if 0
            CPropVariant prop;
            HRESULT hr = m_archiveHandler->GetProperty(index, kpidMTime, &prop);
            if (hr != S_OK)
            {
                _com_issue_error(hr);
            }

            if (prop.vt == VT_EMPTY)
            {
//                m_hasModifiedTime = false;
            }
            else if (prop.vt != VT_FILETIME)
            {
                _com_issue_error(E_FAIL);
            }
            else
            {
 //               m_modifiedTime = prop.filetime;
//                m_hasModifiedTime = true;
            }
#else

            int res = GetArchiveItemInfoHelper::GetPropertyModifiedTime(m_archiveHandler, index, LastWriteTime);
            if (res < 0)
                _com_issue_error(E_FAIL);
            else
                m_hasModifiedTime = (res == 0);

#endif
        }
        void ArchiveExtractCallbackMemory::GetPropertyAccessedTime(UInt32 index)
        {
#if 0
            CPropVariant prop;
            HRESULT hr = m_archiveHandler->GetProperty(index, kpidATime, &prop);
            if (hr != S_OK)
            {
                _com_issue_error(hr);
            }

            if (prop.vt == VT_EMPTY)
            {
                //m_hasAccessedTime = false;
            }
            else if (prop.vt != VT_FILETIME)
            {
                _com_issue_error(E_FAIL);
            }
            else
            {
//                m_accessedTime = prop.filetime;
//                m_hasAccessedTime = true;
            }
#else

            int res = GetArchiveItemInfoHelper::GetPropertyAccessedTime(m_archiveHandler, index, LastAccessTime);
            if (res < 0)
                _com_issue_error(E_FAIL);
            else
                m_hasAccessedTime = (res == 0);

#endif
        }

        void ArchiveExtractCallbackMemory::GetPropertyCreatedTime(UInt32 index)
        {
#if 0
            CPropVariant prop;
            HRESULT hr = m_archiveHandler->GetProperty(index, kpidCTime, &prop);
            if (hr != S_OK)
            {
                _com_issue_error(hr);
            }

            if (prop.vt == VT_EMPTY)
            {
//                m_hasCreatedTime = false;
            }
            else if (prop.vt != VT_FILETIME)
            {
                _com_issue_error(E_FAIL);
            }
            else
            {
  //              m_createdTime = prop.filetime;
  //              m_hasCreatedTime = true;
            }
#else

            int res = GetArchiveItemInfoHelper::GetPropertyCreatedTime(m_archiveHandler, index, CreationTime);
            if (res < 0)
                _com_issue_error(E_FAIL);
            else
                m_hasCreatedTime = (res == 0);

#endif
        }

        void ArchiveExtractCallbackMemory::GetPropertySize(UInt32 index)
        {
#if 0
            CPropVariant prop;
            HRESULT hr = m_archiveHandler->GetProperty(index, kpidSize, &prop);
            if (hr != S_OK)
            {
                _com_issue_error(hr);
            }

            switch (prop.vt)
            {
            case VT_EMPTY:
                //m_hasNewFileSize = false;
                return;
            case VT_UI1:
                Size = prop.bVal;
                break;
            case VT_UI2:
                m_newFileSize = prop.uiVal;
                break;
            case VT_UI4:
                m_newFileSize = prop.ulVal;
                break;
            case VT_UI8:
                m_newFileSize = (UInt64)prop.uhVal.QuadPart;
                break;
            default:
                _com_issue_error(E_FAIL);
            }

            //m_hasNewFileSize = true;
#else

            int res = GetArchiveItemInfoHelper::GetPropertySize(m_archiveHandler, index, Size);
            if (res < 0)
            {
                Size = 0;
                _com_issue_error(E_FAIL);
            }

#endif
        }


        bool ArchiveExtractCallbackMemory::ProcessRollBack()
        {
            bool succ = true;
            for (size_t i = 0; i< m_rollbacks.size(); ++i)
            {
                RollBack_Info& a = m_rollbacks[0];
                if (m_callback)
                    m_callback->OnFileExtractRollBack(a.original_path);

                if (a.backup_path.empty())
                    FileSys::RemovePath(a.original_path);
                else
                    succ = FileSys::RenameFile(a.backup_path, a.original_path);
            }
            return succ;
        }
    }
}
