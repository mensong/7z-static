// Test.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "SevenZip/SevenZipCompressor.h"
#include "SevenZip/SevenZipExtractor.h"
#include "SevenZip/SevenZipExtractorMemory.h"
#include "SevenZip/SevenZipLister.h"

class MyProgressCallback
	: public SevenZip::ProgressCallback
{
public:
	//是否拿文件信息
	virtual bool EnableFilesInfo() 
	{
		return true; 
	}
	//获得文件信息。在EnableFilesInfo返回true时有效。返回true表示继续。返回false表示停止（只获得文件信息）。
	virtual bool OnFileItems(const std::vector<SevenZip::FilePathInfo>& itemsInfo) 
	{
		return true; 
	}

	/*
		开始: 压缩/解压
		totalSize : 待压缩文件总大小  /   压缩包大小? or 待解压文件总大小?
		*/
	virtual void OnStart(const std::wstring& filePath, unsigned __int64 totalSize)
	{
	}

	virtual bool OnItem(const SevenZip::FilePathInfo& itemInfo)
	{
		return true;
	}

	/*
	数值进度 : 压缩/解压
	inSize   : 已处理文件总大小  / 已处理压缩包大小
	outSize  : 生成压缩包大小    / 解压出来文件总大小
	*/
	virtual void OnRadio(const std::wstring& filePath, unsigned __int64  inSize, unsigned __int64 outSize)
	{
	}

	/*
	文件进度 : 压缩/解压
	filePath       : 文件
	bytesCompleted : 已处理大小
	*/
	virtual void OnProgress(const std::wstring& filePath, unsigned __int64 bytesCompleted)
	{
	}

	/*
	压缩/解压完成
	filePath : 压缩包路径
	*/
	virtual void OnEnd(const std::wstring& filePath)
	{
	}

	/*
	解压文件开始
	destFolder : 目标文件夹
	ItemPath   : 相对路径
	return     : 是否继续解压
	*/
	virtual bool OnFileBegin(const std::wstring& destFolder, std::wstring& ItemPath)
	{
		return true;
	}

	/*
	解压文件结束
	filePath       : 文件路径
	bytesCompleted : 文件尺寸
	return         : 是否继续解压
	*/
	virtual bool OnFileDone(const std::wstring& filePath, unsigned __int64 bytesCompleted)
	{
		return true;
	}

	/*
	回滚文件进度
	filePath       : 文件路径
	return         : 是否继续回滚
	*/
	virtual void OnRollBack(const std::wstring& filePath)
	{
	}
};

int main()
{
	HRESULT ret = S_OK;
	SevenZip::SevenZipPassword pwd(true, L"123456");
	MyProgressCallback cb;

	{//压缩文件
		SevenZip::SevenZipCompressor compress;
		compress.SetArchivePath(L"test.7z");
		compress.SetEncryptFileName(true);

		std::vector<SevenZip::TString> files;
		files.push_back(L"tmp\\Win32\\Debug\\Test.ilk");
		files.push_back(L"tmp\\x64");
		files.push_back(L"tmp\\空文件夹");
		if (ret != compress.CompressFiles(L"tmp", files, & cb, &pwd))
		//if (ret != compress.CompressDirectoryWithRoot(L"tmp", L"*", &cb, true, &pwd))
		//if (ret != compress.CompressDirectory(L"D:\\Workspace\\Git\\7z-static\\Test\\tmp", L"*", &cb, true, &pwd))
		//if (compress.CompressFile(L"tmp\\Win32\\Debug\\Test.ilk", &cb, &pwd))
		{
			wprintf_s(L"compress dir to 7z failed\n");
			return 1;
		}
	}

	{//列出文件
		SevenZip::SevenZipLister lister;
		lister.SetArchivePath(L"test.7z");
		if (ret != lister.ListArchive(&cb, &pwd))
		{
			wprintf_s(L"List 7z failed\n");
			return 1;
		}
	}

	{//解压文件到目录
		SevenZip::SevenZipExtractor decompress;
		decompress.SetArchivePath(L"test.7z");
		if (ret != decompress.ExtractArchive(L"unzip", &cb, &pwd))
		{
			wprintf_s(L"decompress 7z to dir failed\n");
			return 1;
		}
	}

	{//解压文件到内存
		SevenZip::SevenZipExtractorMemory extractor;
		extractor.SetArchivePath(L"test.7z");
		CFileStream fs;
		if (ret != extractor.ExtractArchive(fs, &cb, &pwd))
		{
			wprintf_s(L"decompress 7z to memory failed\n");
			return 1;
		}
		int fileCount = fs.GetFileCount();
		for (int i = 0; i < fileCount; i++)
		{
			std::string fileName = fs.GetFileName(i);
			if (fileName == "")
				continue;
			auto fileSize = fs.GetFileSize(fileName.c_str());
			auto fileContent = fs.GetFilePtr(fileName.c_str());
			//处理内存中的文件
		}
	}

	return 0;
}
