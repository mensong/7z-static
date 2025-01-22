#include "SevenZipLister.h"
#include "GUIDs.h"
#include "FileSys.h"
#include "ArchiveOpenCallback.h"
#include "ArchiveExtractCallback.h"
#include "InStreamWrapper.h"
#include "PropVariant2.h"
#include "UsefulFunctions.h"
#include "GetArchiveItemInfoHelper.h"

namespace SevenZip
{

	using namespace intl;

	SevenZipLister::SevenZipLister()
		: SevenZipArchive()
	{
	}

	SevenZipLister::~SevenZipLister()
	{
	}

	HRESULT SevenZipLister::ListArchive(ListCallback* callback, SevenZipPassword* pSevenZipPassword)
	{
		CMyComPtr< IStream > fileStream = FileSys::OpenFileToRead(m_archivePath);
		if (fileStream == NULL)
		{
			return ERROR_OPEN_FAILED;
		}

		return ListArchive(fileStream, callback, pSevenZipPassword);
	}

	HRESULT SevenZipLister::ListArchive(const CMyComPtr< IStream >& archiveStream, ListCallback* callback, SevenZipPassword* pSevenZipPassword)
	{
		CMyComPtr< IInArchive > archive = UsefulFunctions::GetArchiveReader(m_compressionFormat);
		CMyComPtr< InStreamWrapper > inFile = new InStreamWrapper(archiveStream);
		CMyComPtr< ArchiveOpenCallback > openCallback = new ArchiveOpenCallback();

		if (NULL != pSevenZipPassword)
		{
			openCallback->PasswordIsDefined = pSevenZipPassword->PasswordIsDefined;
			openCallback->Password = pSevenZipPassword->Password.c_str();
		}

		HRESULT hr = archive->Open(inFile, 0, openCallback);
		if (hr != S_OK)
		{
			return hr;
		}

		//// List command
		//UInt32 numItems = 0;
		//archive->GetNumberOfItems(&numItems);
		//for (UInt32 i = 0; i < numItems; i++)
		//{
		//	{
		//		// Get uncompressed size of file
		//		CPropVariant prop;
		//		archive->GetProperty(i, kpidSize, &prop);

		//		int size = prop.intVal;

		//		// Get name of file
		//		archive->GetProperty(i, kpidPath, &prop);

		//		//valid string? pass back the found value and call the callback function if set
		//		if (prop.vt == VT_BSTR) {
		//			WCHAR* path = prop.bstrVal;
		//			if (callback) {
		//				callback->OnFileFound(path, size);
		//			}
		//		}
		//	}
		//}
		//CPropVariant prop;
		//archive->GetArchiveProperty(kpidPath,&prop);
		//archive->Close();

		//if (prop.vt == VT_BSTR) {
		//	WCHAR* path = prop.bstrVal;
		//	if (callback) {
		//		callback->OnListingDone(path);
		//	}
		//}

		if (callback && callback->EnableFilesInfo())
		{
			UInt32 mynumofitems = 0;
			hr = archive->GetNumberOfItems(&mynumofitems);
			std::vector<FilePathInfo> fileInfos;
			fileInfos.reserve(mynumofitems);
			for (UInt32 i = 0; i < mynumofitems; ++i)
			{
				FilePathInfo fileInfo;
				GetArchiveItemInfoHelper::GetPropertyFilePath(archive, i, fileInfo.FilePath);
				GetArchiveItemInfoHelper::GetPropertyAttributes(archive, i, fileInfo.Attributes);
				GetArchiveItemInfoHelper::GetPropertyIsDir(archive, i, fileInfo.IsDirectory);
				GetArchiveItemInfoHelper::GetPropertyModifiedTime(archive, i, fileInfo.LastWriteTime);
				GetArchiveItemInfoHelper::GetPropertyAccessedTime(archive, i, fileInfo.LastAccessTime);
				GetArchiveItemInfoHelper::GetPropertyCreatedTime(archive, i, fileInfo.CreationTime);
				GetArchiveItemInfoHelper::GetPropertySize(archive, i, fileInfo.Size);
				fileInfos.push_back(fileInfo);
			}
			if (!callback->OnFileItems(fileInfos))
			{
				//只为了取得文件信息,所以直接返回.
				archive->Close();
				return S_OK;
			}
		}

		return hr;
	}
}