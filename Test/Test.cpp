// Test.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "SevenZip/SevenZipCompressor.h"
#include "SevenZip/SevenZipExtractor.h"
#include "SevenZip/SevenZipExtractorMemory.h"
#include "SevenZip/SevenZipLister.h"

int main()
{
	HRESULT ret = S_OK;
	SevenZip::SevenZipPassword pwd(true, L"123456");

	SevenZip::SevenZipCompressor compress;
	compress.SetArchivePath(L"test.7z");
	compress.SetEncryptFileName(true);	
	if (ret != compress.CompressFiles(L"tmp", L"*.*", NULL, true, &pwd))
	{
		wprintf_s(L"compress dir to 7z failed\n");
		return 1;
	}

	
	SevenZip::SevenZipExtractor decompress;
	decompress.SetArchivePath(L"test.7z");
	if (ret != decompress.ExtractArchive(L"test", NULL, &pwd))
	{
		wprintf_s(L"decompress 7z to dir failed\n");
		return 1;
	}

    return 0;
}
