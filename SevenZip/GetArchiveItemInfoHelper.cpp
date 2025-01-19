#include "GetArchiveItemInfoHelper.h"
#include "PropVariant2.h"
#include <comdef.h>

namespace SevenZip
{
namespace intl
{
    const TString EmptyFileAlias = _T("[Content]");

    bool GetArchiveItemInfoHelper::GetPropertyFilePath(CMyComPtr<IInArchive> archive, UInt32 index, TString& outFilePath)
    {
        CPropVariant prop;
        HRESULT hr = archive->GetProperty(index, kpidPath, &prop);
        if (hr != S_OK)
        {
            //_com_issue_error(hr);
            return false;
        }

        if (prop.vt == VT_EMPTY)
        {
            outFilePath = EmptyFileAlias;
        }
        else if (prop.vt != VT_BSTR)
        {
            //_com_issue_error(E_FAIL);
            return false;
        }
        else
        {
            _bstr_t bstr = prop.bstrVal;
#ifdef _UNICODE
            outFilePath = bstr;
#else
            char relPath[MAX_PATH];
            int size = WideCharToMultiByte(CP_UTF8, 0, bstr, bstr.length(), relPath, MAX_PATH, NULL, NULL);
            outFilePath.assign(relPath, size);
#endif
        }
        return true;
    }

    int GetArchiveItemInfoHelper::GetPropertyAttributes(CMyComPtr<IInArchive> archive, UInt32 index, UINT& outAttributes)
    {
        CPropVariant prop;
        HRESULT hr = archive->GetProperty(index, kpidAttrib, &prop);
        if (hr != S_OK)
        {
            //_com_issue_error(hr);
            return -1;
        }

        if (prop.vt == VT_EMPTY)
        {
            //m_attrib = 0;
            //m_hasAttrib = false;
            outAttributes = 0;
            return 1;
        }
        else if (prop.vt != VT_UI4)
        {
            //_com_issue_error(E_FAIL);
            return -2;
        }
        else
        {
            //m_attrib = prop.ulVal;
            //m_hasAttrib = true;
            outAttributes = prop.ulVal;
        }
        return 0;
    }

    int GetArchiveItemInfoHelper::GetPropertyIsDir(CMyComPtr<IInArchive> archive, UInt32 index, bool& outIsDirectory)
    {
        CPropVariant prop;
        HRESULT hr = archive->GetProperty(index, kpidIsDir, &prop);
        if (hr != S_OK)
        {
            //_com_issue_error(hr);
            return -1;
        }

        if (prop.vt == VT_EMPTY)
        {
            outIsDirectory = false;
        }
        else if (prop.vt != VT_BOOL)
        {
            //_com_issue_error(E_FAIL);
            return -2;
        }
        else
        {
            //m_isDir = prop.boolVal != VARIANT_FALSE;
            outIsDirectory = prop.boolVal != VARIANT_FALSE;
        }
        return 0;
    }

    int GetArchiveItemInfoHelper::GetPropertyModifiedTime(CMyComPtr<IInArchive> archive, UInt32 index, FILETIME& outModifiedTime)
    {
        CPropVariant prop;
        HRESULT hr = archive->GetProperty(index, kpidMTime, &prop);
        if (hr != S_OK)
        {
            //_com_issue_error(hr);
            return -1;
        }

        if (prop.vt == VT_EMPTY)
        {
            return 1;
        }
        else if (prop.vt != VT_FILETIME)
        {
            //_com_issue_error(E_FAIL);
            return -2;
        }
        else
        {
            outModifiedTime = prop.filetime;
        }
        return 0;
    }
    int GetArchiveItemInfoHelper::GetPropertyAccessedTime(CMyComPtr<IInArchive> archive, UInt32 index, FILETIME& outAccessedTime)
    {
        CPropVariant prop;
        HRESULT hr = archive->GetProperty(index, kpidATime, &prop);
        if (hr != S_OK)
        {
            //_com_issue_error(hr);
            return -1;
        }

        if (prop.vt == VT_EMPTY)
        {
            return 1;
        }
        else if (prop.vt != VT_FILETIME)
        {
            //_com_issue_error(E_FAIL);
            return -2;
        }
        else
        {
            outAccessedTime = prop.filetime;
        }
        return 0;
    }

    int GetArchiveItemInfoHelper::GetPropertyCreatedTime(CMyComPtr<IInArchive> archive, UInt32 index, FILETIME& outCreatedTime)
    {
        CPropVariant prop;
        HRESULT hr = archive->GetProperty(index, kpidCTime, &prop);
        if (hr != S_OK)
        {
            //_com_issue_error(hr);
            return -1;
        }

        if (prop.vt == VT_EMPTY)
        {
            return 1;
        }
        else if (prop.vt != VT_FILETIME)
        {
            //_com_issue_error(E_FAIL);
            return -2;
        }
        else
        {
            outCreatedTime = prop.filetime;
        }
        return 0;
    }

    int GetArchiveItemInfoHelper::GetPropertySize(CMyComPtr<IInArchive> archive, UInt32 index, ULONGLONG& outSize)
    {
        CPropVariant prop;
        HRESULT hr = archive->GetProperty(index, kpidSize, &prop);
        if (hr != S_OK)
        {
            //_com_issue_error(hr);
            return -1;
        }

        switch (prop.vt)
        {
        case VT_EMPTY:
            return 1;
        case VT_UI1:
            //m_newFileSize = prop.bVal;
            outSize = prop.bVal;
            break;
        case VT_UI2:
            //m_newFileSize = prop.uiVal;
            outSize = prop.uiVal;
            break;
        case VT_UI4:
            //m_newFileSize = prop.ulVal;
            outSize = prop.ulVal;
            break;
        case VT_UI8:
            outSize = (UInt64)prop.uhVal.QuadPart;
            break;
        default:
            //_com_issue_error(E_FAIL);
            return -2;
        }
        return 0;
    }
}
}
