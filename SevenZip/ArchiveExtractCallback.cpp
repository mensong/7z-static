﻿#include "ArchiveExtractCallback.h"
#include "PropVariant2.h"
#include "FileSys.h"
#include "OutStreamWrapper.h"
#include <comdef.h>
#include "../CPP/Common/MyCom.h"
#include "GetArchiveItemInfoHelper.h"

namespace SevenZip
{
    namespace intl
    {

        const TString EmptyFileAlias = _T("[Content]");


		ArchiveExtractCallback::ArchiveExtractCallback(
			const CMyComPtr< IInArchive >& archiveHandler,
			const TString& archivePath,
			const TString& directory,
			OverwriteModeEnum mode,
			ProgressCallback* callback)
			: m_archivePath(archivePath)
			, m_directory(directory)
			, m_refCount(0)
			, m_archiveHandler(archiveHandler)
			, m_callback(callback)
			, m_overwriteMode(mode)
			, PasswordIsDefined(false)
        {
            if (*m_directory.rbegin() != L'\\' && *m_directory.rbegin() != L'/')
				m_directory += L'\\';
            //把目录转为绝对路径
            m_directory = FileSys::GetAbsolutePath(m_directory);
        }

        ArchiveExtractCallback::~ArchiveExtractCallback()
        {
        }

        STDMETHODIMP ArchiveExtractCallback::QueryInterface(REFIID iid, void** ppvObject)
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

        STDMETHODIMP_(ULONG) ArchiveExtractCallback::AddRef()
        {
            return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
        }

        STDMETHODIMP_(ULONG) ArchiveExtractCallback::Release()
        {
            ULONG res = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
            if (res == 0)
            {
                delete this;
            }
            return res;
        }

        STDMETHODIMP ArchiveExtractCallback::SetTotal(UInt64 size)
        {
//#ifdef _DEBUG
//            wprintf_s(L"SetTotal:%llu\n", size);
//#endif // _DEBUG

            m_totalSize = size;
            //	- SetTotal is never called for ZIP and 7z formats
            if (m_callback)
            {
                m_callback->OnWorkStart(m_archivePath, size);
            }
            return S_OK;
        }

        STDMETHODIMP ArchiveExtractCallback::SetCompleted(const UInt64* completeValue)
        {
//#ifdef _DEBUG
//            wprintf_s(L"SetCompleted:%llu\n", *completeValue);
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
                //m_callback->OnWorkProgress(m_directory, *completeValue);
            }
            return S_OK;
        }

        STDMETHODIMP ArchiveExtractCallback::SetRatioInfo(const UInt64 *inSize, const UInt64 *outSize)
        {
//#ifdef _DEBUG
//            wprintf_s(L"SetRatioInfo:%llu-%llu\n", *inSize, *outSize);
//#endif // _DEBUG

            if (m_callback)
                m_callback->OnWorkRadio(m_directory, *inSize, *outSize);
            return S_OK;
        }

        STDMETHODIMP ArchiveExtractCallback::GetStream(UInt32 index, ISequentialOutStream** outStream, Int32 askExtractMode)
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

            m_index = index;
            // TODO: m_directory could be a relative path as "..\"
            m_absPath = FileSys::AppendPath(m_directory, FilePath);

            if (m_callback)
            {
                m_callback->OnWorkItemInfo(*((FilePathInfo*)this));
            }

            if (askExtractMode != NArchive::NExtract::NAskMode::kExtract)
                return S_OK;

            if (IsDirectory)
            {
                FileSys::CreateDirectoryTree(m_absPath);
                *outStream = NULL;
                return S_OK;
            }

            if (m_callback)
            {
                if (!m_callback->OnFileExtractBegin(m_directory, FilePath))
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

            if (m_overwriteMode == OverwriteMode::kRollBack)
            {
                TString BackupPath;
                if (FileSys::PathExists(m_absPath))
                {
                    BackupPath = FileSys::GetUniquePath(m_absPath);
                    if (!FileSys::BackupFile(m_absPath, BackupPath))
                        return HRESULT_FROM_WIN32(GetLastError());
                }
                RollBack_Info info;
                info.backup_path = BackupPath;
                info.original_path = m_absPath;
                m_rollbacks.push_back(info);
            }
            else if (FileSys::PathExists(m_absPath))
            {
                if (m_overwriteMode == OverwriteMode::kSkipExisting)
                {
                    *outStream = NULL;
                    return S_OK;
                }
                else if (m_overwriteMode == OverwriteMode::kAutoRename)
                {
                    m_absPath = FileSys::GetUniquePath(m_absPath);
                }
                else if (m_overwriteMode == OverwriteMode::kAutoRenameExisting)
                {
                    TString rename_path = FileSys::GetUniquePath(m_absPath);
                    if (!FileSys::RenameFile(m_absPath, rename_path))
                    {
                        wprintf_s(L"重命名文件失败:%s-%s\n", m_absPath.c_str(), rename_path.c_str());
                        return HRESULT_FROM_WIN32(GetLastError());
                    }
                }
                else if (m_overwriteMode == OverwriteMode::kWithoutPrompt)
                {
                    if (!FileSys::RemovePath(m_absPath))
                    {
                        wprintf_s(L"移除路径失败:%s\n", m_absPath.c_str());
                        return HRESULT_FROM_WIN32(GetLastError());
                    }
                }
                else
                {
                    wprintf_s(L"文件已存在:%s\n", m_absPath.c_str());
                    return ERROR_FILE_EXISTS;
                }
            }

            TString absDir = FileSys::GetPath(m_absPath);
            if (!FileSys::CreateDirectoryTree(absDir))
            {
                wprintf_s(L"创建目录失败:%s\n", absDir.c_str());
                return ERROR_CREATE_FAILED;
            }

            CMyComPtr< IStream > fileStream = FileSys::OpenFileToWrite(m_absPath);
            if (fileStream == NULL)
            {
                wprintf_s(L"创建文件失败:%s\n", m_absPath.c_str());
                m_absPath.clear();
                return HRESULT_FROM_WIN32(GetLastError());
            }

            OutStreamWrapper* stream = new OutStreamWrapper(fileStream);
            if (!stream)
            {
                wprintf_s(L"内存不足\n");
                return E_OUTOFMEMORY;
            }

            CMyComPtr< OutStreamWrapper > wrapperStream = stream;
            *outStream = wrapperStream.Detach();

            return S_OK;
        }

        STDMETHODIMP ArchiveExtractCallback::PrepareOperation(Int32 /*askExtractMode*/)
        {
            return S_OK;
        }

        STDMETHODIMP ArchiveExtractCallback::SetOperationResult(Int32 operationResult)
        {
            if (m_overwriteMode == OverwriteMode::kRollBack && operationResult != S_OK)
            {
                //_ASSERT_EXPR(FALSE,L"begin rollback");
                bool succ = ProcessRollBack(); succ;
                //_ASSERT_EXPR(succ, L"rollback error!");
                return E_FAIL;
            }
            

            if (m_absPath.empty())
            {
                if (m_callback)
                    m_callback->OnWorkEnd(m_directory);
                return S_OK;
            }

            if (m_hasModifiedTime || m_hasAccessedTime || m_hasCreatedTime)
            {
                HANDLE fileHandle = CreateFile(m_absPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                if (fileHandle != INVALID_HANDLE_VALUE)
                {
                    SetFileTime(fileHandle,
                        m_hasCreatedTime ? &CreationTime : NULL,
                        m_hasAccessedTime ? &LastAccessTime : NULL,
                        m_hasModifiedTime ? &LastWriteTime : NULL);
                    CloseHandle(fileHandle);
                }
            }

            if (m_hasAttrib)
            {
                SetFileAttributes(m_absPath.c_str(), Attributes);
            }

            if (m_callback)
            {
                if(!m_callback->OnFileExtractDone(m_absPath, Size))
                    return E_FAIL;
                m_callback->OnWorkProgress(m_absPath, Size);
            }
            return S_OK;
        }

        STDMETHODIMP ArchiveExtractCallback::CryptoGetTextPassword(BSTR* password)
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

        void ArchiveExtractCallback::GetPropertyFilePath(UInt32 index)
        {
//            CPropVariant prop;
//            HRESULT hr = m_archiveHandler->GetProperty(index, kpidPath, &prop);
//            if (hr != S_OK)
//            {
//                _com_issue_error(hr);
//            }
//
//            if ( prop.vt == VT_EMPTY )
//            {
//                FilePath = EmptyFileAlias;
//            }
//            else if (prop.vt != VT_BSTR)
//            {
//                _com_issue_error(E_FAIL);
//            }
//            else
//            {
//                _bstr_t bstr = prop.bstrVal;
//#ifdef _UNICODE
//                FilePath = bstr;
//#else
//                char relPath[MAX_PATH];
//                int size = WideCharToMultiByte(CP_UTF8, 0, bstr, bstr.length(), relPath, MAX_PATH, NULL, NULL);
//                FilePath.assign(relPath, size);
//#endif
//            }

            if (!GetArchiveItemInfoHelper::GetPropertyFilePath(m_archiveHandler, index, FilePath))
                _com_issue_error(E_FAIL);
        }

        void ArchiveExtractCallback::GetPropertyAttributes(UInt32 index)
        {
            //CPropVariant prop;
            //HRESULT hr = m_archiveHandler->GetProperty(index, kpidAttrib, &prop);
            //if (hr != S_OK)
            //{
            //    _com_issue_error(hr);
            //}

            //if (prop.vt == VT_EMPTY)
            //{
            //    //m_attrib = 0;
            //    //m_hasAttrib = false;
            //    Attributes = 0;
            //}
            //else if (prop.vt != VT_UI4)
            //{
            //    _com_issue_error(E_FAIL);
            //}
            //else
            //{
            //    //m_attrib = prop.ulVal;
            //    //m_hasAttrib = true;
            //    Attributes = prop.ulVal;
            //}

            int res = GetArchiveItemInfoHelper::GetPropertyAttributes(m_archiveHandler, index, Attributes);
            if (res < 0)
                _com_issue_error(E_FAIL);
            else
            {
                m_hasAttrib = (res == 0);
            }
        }

        void ArchiveExtractCallback::GetPropertyIsDir(UInt32 index)
        {
            //CPropVariant prop;
            //HRESULT hr = m_archiveHandler->GetProperty(index, kpidIsDir, &prop);
            //if (hr != S_OK)
            //{
            //    _com_issue_error(hr);
            //}

            //if (prop.vt == VT_EMPTY)
            //{
            //    IsDirectory = false;
            //}
            //else if (prop.vt != VT_BOOL)
            //{
            //    _com_issue_error(E_FAIL);
            //}
            //else
            //{
            //    //m_isDir = prop.boolVal != VARIANT_FALSE;
            //    IsDirectory = prop.boolVal != VARIANT_FALSE;
            //}

            int res = GetArchiveItemInfoHelper::GetPropertyIsDir(m_archiveHandler, index, IsDirectory);
            if (res < 0)
                _com_issue_error(E_FAIL);
        }

        void ArchiveExtractCallback::GetPropertyModifiedTime(UInt32 index)
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
                m_hasModifiedTime = false;
                LastWriteTime = { 0 };
            }
            else if (prop.vt != VT_FILETIME)
            {
                _com_issue_error(E_FAIL);
            }
            else
            {
                //m_modifiedTime = prop.filetime;
                m_hasModifiedTime = true;
                LastWriteTime = prop.filetime;
            }
#else
            int res = GetArchiveItemInfoHelper::GetPropertyModifiedTime(m_archiveHandler, index, LastWriteTime);
            if (res < 0)
                _com_issue_error(E_FAIL);
            else
                m_hasModifiedTime = (res == 0);
#endif
        }
        void ArchiveExtractCallback::GetPropertyAccessedTime(UInt32 index)
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
                m_hasAccessedTime = false;
				LastAccessTime = { 0 };
            }
            else if (prop.vt != VT_FILETIME)
            {
                _com_issue_error(E_FAIL);
            }
            else
            {
                //m_accessedTime = prop.filetime;
                m_hasAccessedTime = true;
                LastAccessTime = prop.filetime;
            }
#else
            int res = GetArchiveItemInfoHelper::GetPropertyAccessedTime(m_archiveHandler, index, LastAccessTime);
            if (res < 0)
                _com_issue_error(E_FAIL);
            else
                m_hasAccessedTime = (res == 0);
#endif
        }

        void ArchiveExtractCallback::GetPropertyCreatedTime(UInt32 index)
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
                m_hasCreatedTime = false;
                CreationTime = { 0 };
            }
            else if (prop.vt != VT_FILETIME)
            {
                _com_issue_error(E_FAIL);
            }
            else
            {
                //m_createdTime = prop.filetime;
                m_hasCreatedTime = true;
                CreationTime = prop.filetime;
            }
#else
            int res = GetArchiveItemInfoHelper::GetPropertyCreatedTime(m_archiveHandler, index, CreationTime);
            if (res < 0)
                _com_issue_error(E_FAIL);
            else
                m_hasCreatedTime = (res == 0);
#endif
        }

        void ArchiveExtractCallback::GetPropertySize(UInt32 index)
        {
            //CPropVariant prop;
            //HRESULT hr = m_archiveHandler->GetProperty(index, kpidSize, &prop);
            //if (hr != S_OK)
            //{
            //    _com_issue_error(hr);
            //}

            //switch (prop.vt)
            //{
            //case VT_EMPTY:
            //    //m_hasNewFileSize = false;
            //    Size = 0;
            //    return;
            //case VT_UI1:
            //    //m_newFileSize = prop.bVal;
            //    Size = prop.bVal;
            //    break;
            //case VT_UI2:
            //    //m_newFileSize = prop.uiVal;
            //    Size = prop.uiVal;
            //    break;
            //case VT_UI4:
            //    //m_newFileSize = prop.ulVal;
            //    Size = prop.ulVal;
            //    break;
            //case VT_UI8:
            //    //m_newFileSize = (UInt64)prop.uhVal.QuadPart;
            //    Size = (UInt64)prop.uhVal.QuadPart;
            //    break;
            //default:
            //    _com_issue_error(E_FAIL);
            //}

            ////m_hasNewFileSize = true;

            int res = GetArchiveItemInfoHelper::GetPropertySize(m_archiveHandler, index, Size);
            if (res < 0)
                _com_issue_error(E_FAIL);
        }


        bool ArchiveExtractCallback::ProcessRollBack()
        {
            bool succ = true;
            for (size_t i = 0;i<m_rollbacks.size();++i)
            {
                RollBack_Info& a = m_rollbacks[i];
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
