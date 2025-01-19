#pragma once
#include "FileInfo.h"
#include "../CPP/7zip/Archive/IArchive.h"
#include "../CPP/Common/MyCom.h"

namespace SevenZip
{
namespace intl
{
    class GetArchiveItemInfoHelper
    {
    public:
        static bool GetPropertyFilePath(CMyComPtr<IInArchive> archive, UInt32 index, TString& outFilePath);

        //return <0 if error; return 0 if ok; return 1 if property not supported;
        static int GetPropertyAttributes(CMyComPtr<IInArchive> archive, UInt32 index, UINT& outAttributes);
        static int GetPropertyIsDir(CMyComPtr<IInArchive> archive, UInt32 index, bool& outIsDirectory);
        static int GetPropertyModifiedTime(CMyComPtr<IInArchive> archive, UInt32 index, FILETIME& outModifiedTime);
        static int GetPropertyAccessedTime(CMyComPtr<IInArchive> archive, UInt32 index, FILETIME& outAccessedTime);
        static int GetPropertyCreatedTime(CMyComPtr<IInArchive> archive, UInt32 index, FILETIME& outCreatedTime);
        static int GetPropertySize(CMyComPtr<IInArchive> archive, UInt32 index, ULONGLONG& outSize);
    };
}
}
