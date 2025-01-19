#pragma once
#include "SevenString.h"
#include "../CPP/Common/MyWindows.h"

namespace SevenZip
{
	class FileInfo
	{
	public:
		FileInfo()
		{
			LastWriteTime = { 0 };
			CreationTime = { 0 };
			LastAccessTime = { 0 };
			Size = 0;
			Attributes = 0;
			IsDirectory = false;
		}

		TString		FileName;
		FILETIME	LastWriteTime;
		FILETIME	CreationTime;
		FILETIME	LastAccessTime;
		ULONGLONG	Size;
		UINT		Attributes;
		bool		IsDirectory;
	};

	class FilePathInfo : public FileInfo
	{
	public:
		TString		FilePath;
	};
}
