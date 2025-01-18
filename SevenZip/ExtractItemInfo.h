#pragma once
#ifndef _WIN32
#include <wctype.h>
#include <wchar.h>
#endif

#include "../CPP/Common/MyWindows.h"
#include "../CPP/Common/MyTypes.h"

#include "SevenString.h"

namespace SevenZip
{
    class ExtractItemInfo
    {
    public:
		ExtractItemInfo(const TString& directory)
			: m_index(-1)
			, m_directory(directory)
			, m_isDir(false)
			, m_hasAttrib(false)
			, m_attrib(0)
			, m_hasModifiedTime(false)
			, m_hasAccessedTime(false)
			, m_hasCreatedTime(false)
			, m_hasNewFileSize(false)
			, m_totalSize(0)
		{

		}

		int m_index;

		TString m_directory;

		TString m_relPath;
		TString m_absPath;
		bool m_isDir;

		bool m_hasAttrib;
		UInt32 m_attrib;

		bool m_hasModifiedTime;
		FILETIME m_modifiedTime;
		bool m_hasAccessedTime;
		FILETIME m_accessedTime;
		bool m_hasCreatedTime;
		FILETIME m_createdTime;

		bool m_hasNewFileSize;
		UInt64 m_newFileSize;

		UInt64 m_totalSize;

    };
}
