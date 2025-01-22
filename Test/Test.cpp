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
	//返回文件信息。在EnableFilesInfo返回true时有效。
	virtual bool OnFileItems(const std::vector<SevenZip::FilePathInfo>& itemsInfo)
	{
		return true;
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
		{
			wprintf_s(L"compress dir to 7z failed\n");
			return 1;
		}
		//compress.CompressFile(L"E:\\2.mdb", &cb, &pwd);
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
