#pragma once
#include <vector>
#include "FileInfo.h"

namespace SevenZip
{
	class ListCallback
	{
	public:
		//�Ƿ����ļ���Ϣ
		virtual bool EnableFilesInfo() { return false; }
		//����ļ���Ϣ����EnableFilesInfo����trueʱ��Ч������true��ʾ����������false��ʾֹͣ��ֻ����ļ���Ϣ����
		virtual bool OnFileItems(const std::vector<SevenZip::FilePathInfo>& itemsInfo) { return true; }
	};
}
