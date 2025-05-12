// PerformanceTesting.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include <windows.h>
#include <tchar.h>
#include "SevenZip/SevenZipCompressor.h"
#include "SevenZip/SevenZipExtractor.h"
#include "SevenZip/SevenZipExtractorMemory.h"
#include "SevenZip/SevenZipLister.h"

#pragma comment(lib, "version.lib")

std::wstring AnsiToUnicode(const std::string& multiByteStr, UINT codePage = 936)
{
	wchar_t* pWideCharStr; //定义返回的宽字符指针
	int nLenOfWideCharStr; //保存宽字符个数，注意不是字节数
	const char* pMultiByteStr = multiByteStr.c_str();
	//获取宽字符的个数
	nLenOfWideCharStr = MultiByteToWideChar(codePage, 0, pMultiByteStr, -1, NULL, 0);
	//获得宽字符指针
	pWideCharStr = (wchar_t*)(HeapAlloc(GetProcessHeap(), 0, nLenOfWideCharStr * sizeof(wchar_t)));
	MultiByteToWideChar(codePage, 0, pMultiByteStr, -1, pWideCharStr, nLenOfWideCharStr);
	//返回
	std::wstring wideByteRet(pWideCharStr, nLenOfWideCharStr);
	//销毁内存中的字符串
	HeapFree(GetProcessHeap(), 0, pWideCharStr);
	return wideByteRet.c_str();
}

int main(int argc, char** argv)
{
	// 获取处理器信息
	SYSTEM_INFO sysinfo = { 0 };
	GetSystemInfo(&sysinfo);
	std::cout 
		<< "CPU核数:\t" << sysinfo.dwNumberOfProcessors << std::endl
		<< "CPU频率:\t" << sysinfo.dwProcessorType << " MHz" << std::endl;

	// 获取内存信息
	MEMORYSTATUSEX meminfo = { 0 };
	meminfo.dwLength = sizeof(MEMORYSTATUSEX);
	GlobalMemoryStatusEx(&meminfo);
	std::cout << "内存:\t\t" << meminfo.ullTotalPhys / (1024 * 1024) << " MB" << std::endl;

	if (argc < 3)
	{
		std::cout << "PerformanceTesting.exe DirPath ZipFile [CompressionLevel 0-9]" << std::endl;
		return -1;
	}

	std::wstring dir = AnsiToUnicode(argv[1]);
	std::wstring zipFile = AnsiToUnicode(argv[2]);
	int CompressionLevel = 0;

	if (argc == 4)
	{
		CompressionLevel = std::atoi(argv[3]);
		if (CompressionLevel < 0 || CompressionLevel > 9)
			CompressionLevel = 0;
	}

	{
		DWORD st = ::GetTickCount();

		SevenZip::SevenZipCompressor compress;
		compress.SetArchivePath(zipFile.c_str());
		compress.SetCompressionLevel((SevenZip::CompressionLevel::_Enum)CompressionLevel);
		if (S_OK != compress.CompressDirectory(dir.c_str()))
		{
			wprintf_s(L"compress dir to 7z failed\n");
			return 1;
		}

		DWORD t = ::GetTickCount() - st;
		std::cout << "Compress cast time:\t" << t / 1000.0 << "秒" << std::endl;
	}

	{
		DWORD st = ::GetTickCount();

		SevenZip::SevenZipExtractorMemory extractor;
		extractor.SetArchivePath(zipFile.c_str());
		CFileStream fs;
		if (S_OK != extractor.ExtractArchive(fs, NULL, NULL))
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

		DWORD t = ::GetTickCount() - st;
		std::cout << "Extractor to memory cast time:\t" << t / 1000.0 << "秒" << std::endl;
	}

	{
		std::wstring _dir = dir;
		if (_dir[_dir.size() - 1] == L'\\' || _dir[_dir.size() - 1] == L'/')
			_dir.pop_back();
		_dir += L"_unzip";

		DWORD st = ::GetTickCount();

		SevenZip::SevenZipExtractor decompress;
		decompress.SetArchivePath(zipFile.c_str());
		if (S_OK != decompress.ExtractArchive(_dir.c_str(), NULL, NULL))
		{
			wprintf_s(L"decompress 7z to dir failed\n");
			return 1;
		}

		DWORD t = ::GetTickCount() - st;
		std::cout << "Extractor to disk cast time:\t" << t / 1000.0 << "秒" << std::endl;
	}

    return 0;
}
