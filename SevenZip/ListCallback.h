#pragma once
#include <vector>
#include "FileInfo.h"

namespace SevenZip
{
	class ListCallback
	{
	public:
		//是否拿文件信息
		virtual bool EnableFilesInfo() { return false; }
		//获得文件信息。在EnableFilesInfo返回true时有效。返回true表示继续。返回false表示停止（只获得文件信息）。
		virtual bool OnFileItems(const std::vector<SevenZip::FilePathInfo>& itemsInfo) { return true; }
	};
}
