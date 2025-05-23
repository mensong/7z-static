﻿#include "SevenZipCompressor.h"
#include "GUIDs.h"
#include "FileSys.h"
#include "ArchiveUpdateCallback.h"
#include "OutStreamWrapper.h"
#include "PropVariant2.h"
#include "UsefulFunctions.h"


namespace SevenZip
{

using namespace intl;



SevenZipCompressor::SevenZipCompressor()
	: SevenZipArchive()
{
}

SevenZipCompressor::~SevenZipCompressor()
{
}

HRESULT SevenZipCompressor::CompressDirectoryWithRoot(
	const TString& directory, const TString& searchFilter, ProgressCallback* callback, bool includeSubdirs, SevenZipPassword *pSevenZipPassword)
{	
	return FindAndCompressFiles( 
				directory, 
				searchFilter,
				FileSys::GetPath( directory ), 
				includeSubdirs,
				callback,
				pSevenZipPassword);
}

HRESULT SevenZipCompressor::CompressDirectory(
	const TString& directory, const TString& searchFilter, ProgressCallback* callback, bool includeSubdirs, SevenZipPassword *pSevenZipPassword)
{
	return FindAndCompressFiles( 
				directory, 
				searchFilter, 
				directory, 
				includeSubdirs,
				callback,
				pSevenZipPassword);
}

HRESULT SevenZipCompressor::CompressFile(const TString& filePath, ProgressCallback* callback, SevenZipPassword *pSevenZipPassword)
{
	std::vector< FilePathInfo > files = FileSys::GetFile( filePath );

	if ( files.empty() )
	{
        return ERROR_EMPTY;
	}

	m_srcPath = filePath;

	return CompressFilesToArchive(TString(), files, callback, pSevenZipPassword);
}

HRESULT SevenZipCompressor::CompressFiles(
	const TString& pathPrefix,
	const std::vector<TString>& files,
	ProgressCallback* callback, 
	SevenZipPassword* pSevenZipPassword)
{
	TString pathPrefix_ = pathPrefix;
	if (!pathPrefix_.empty() && *pathPrefix_.rbegin() != L'/' && *pathPrefix_.rbegin() != L'\\')
		pathPrefix_ += L'\\';

	std::vector< FilePathInfo > filesToCompress;
	for (size_t i = 0; i < files.size(); i++)
	{
		std::vector< FilePathInfo > fileInfo;
		if (FileSys::FileExists(files[i]))
		{//文件
			fileInfo = FileSys::GetFile(files[i]);
		}
		else
		{//文件夹
			fileInfo = FileSys::GetFilesInDirectory(files[i], L"*", true);
			if (fileInfo.size() == 0)
			{//空文件夹
				fileInfo = FileSys::GetDirectoryInfo(files[i]);
			}
		}
		filesToCompress.insert(filesToCompress.end(), fileInfo.begin(), fileInfo.end());
	}

	return CompressFilesToArchive(pathPrefix_, filesToCompress, callback, pSevenZipPassword);
}

CMyComPtr< IStream > SevenZipCompressor::OpenArchiveStream()
{
	CMyComPtr< IStream > fileStream = FileSys::OpenFileToWrite( m_archivePath );
	if ( fileStream == NULL )
	{
		return NULL;
	}
	return fileStream;
}

HRESULT SevenZipCompressor::FindAndCompressFiles(
	const TString& directory, 
	const TString& searchPattern, 
	const TString& pathPrefix_, 
	bool recursion, 
	ProgressCallback* callback, 
	SevenZipPassword *pSevenZipPassword)
{
    //修正压缩包里面有空的顶级文件夹的的情况
    TString pathPrefix = pathPrefix_;
	if (!pathPrefix.empty() && *pathPrefix.rbegin() != L'/' && *pathPrefix.rbegin() != L'\\')
        pathPrefix += L'\\';

	if ( !FileSys::DirectoryExists( directory ) )
	{
		return HRESULT_FROM_WIN32(GetLastError());	//Directory does not exist
	}
	
	//if ( FileSys::IsDirectoryEmptyRecursive( directory ) )
	//{
    //    return ERROR_EMPTY;	//Directory \"%s\" is empty" ), directory.c_str()
	//}

	m_srcPath = directory;

	std::vector< FilePathInfo > files = FileSys::GetFilesInDirectory( directory, searchPattern, recursion );
    //if (callback)
    //{
    //    if (callback->OnFileCount(files.size()))
    //    {
    //        std::vector<std::wstring> itemNames;
    //        itemNames.reserve(files.size());
    //        std::vector<unsigned __int64> itemSizes;
    //        itemSizes.reserve(files.size());

    //        for (size_t i = 0; i< files.size(); ++i)
    //        {
    //            FilePathInfo& a = files[i];
    //            itemNames.push_back(a.FilePath);
    //            itemSizes.push_back(a.Size);
    //        }
    //        if (!callback->OnFileItems(itemNames, itemSizes))
    //            return S_OK;
    //    }
    //}
	return CompressFilesToArchive(pathPrefix, files, callback, pSevenZipPassword);
}

HRESULT SevenZipCompressor::CompressFilesToArchive(const TString& pathPrefix, const std::vector< FilePathInfo >& filePaths,
	ProgressCallback* progressCallback, SevenZipPassword *pSevenZipPassword)
{
	//信息回调
	if (progressCallback && progressCallback->EnableFilesInfo())
	{
		if (!progressCallback->OnFileItems(filePaths))
		{
			return S_OK;
		}
	}

    CMyComPtr<IOutArchive> archiver = UsefulFunctions::GetArchiveWriter(m_compressionFormat);
	SetCompressionProperties( archiver );

	m_srcPath = m_archivePath;

    CMyComPtr<OutStreamWrapper> outFile = new OutStreamWrapper(OpenArchiveStream());
    CMyComPtr<ArchiveUpdateCallback> updateCallback = new ArchiveUpdateCallback(pathPrefix, filePaths, m_srcPath, progressCallback);
	
	if (pSevenZipPassword)
	{
		updateCallback->PasswordIsDefined = pSevenZipPassword->PasswordIsDefined;
		updateCallback->Password = pSevenZipPassword->Password.c_str();
	}

	HRESULT hr = archiver->UpdateItems( outFile, (UInt32) filePaths.size(), updateCallback );
	

	if (progressCallback)
	{
		progressCallback->OnWorkEnd(m_srcPath);	//Todo: give full path support
	}

    return hr;
}

HRESULT SevenZipCompressor::SetCompressionProperties(IUnknown* outArchive)
{
	CMyComPtr< ISetProperties > setter;
	outArchive->QueryInterface(IID_ISetProperties, reinterpret_cast< void** >(&setter));
	if (setter == NULL)
	{
		return E_NOTIMPL;	//Archive does not support setting compression properties
	}

	if (m_EncryptFileName)
	{
		const size_t numProps = 1;
		const wchar_t* names[numProps] = { L"he" };
		CPropVariant values[numProps] = { L"ON"};
		
		HRESULT ret =  setter->SetProperties(names, values, numProps);
		if (S_OK != ret)
			return ret;
	}

	if (m_compressionLevel == CompressionLevel::Default)
	{ 
		return S_OK;
	}
	else
	{
		const size_t numProps = 1;
		const wchar_t* names[numProps] = { L"x" };
		CPropVariant values[numProps] = { static_cast< UInt32 >(m_compressionLevel.GetValue()) };
		  
		return setter->SetProperties(names, values, numProps);
	} 
}
}
