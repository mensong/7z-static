#pragma once
#include <string>
#include "ListCallback.h"

namespace SevenZip
{
    class ProgressCallback
        : public ListCallback
    {
    public:
        /*
        开始: 压缩/解压
        totalSize : 待压缩文件总大小  /   压缩包大小? or 待解压文件总大小?
        */
        virtual void OnWorkStart(const std::wstring& filePath, unsigned __int64 totalSize) 
        {
        }

        /*
        压缩/解压每一个压缩对象
        */
        virtual bool OnWorkItemInfo(const SevenZip::FilePathInfo& itemInfo)
        {
            return true;
        }

        /*
        数值进度 : 压缩/解压
        inSize   : 已处理文件总大小  / 已处理压缩包大小
        outSize  : 生成压缩包大小    / 解压出来文件总大小
        */
        virtual void OnWorkRadio(const std::wstring& filePath, unsigned __int64  inSize, unsigned __int64 outSize)
        {
        }

        /*
        文件进度 : 压缩/解压
        filePath       : 文件
        bytesCompleted : 已处理大小
        */
        virtual void OnWorkProgress(const std::wstring& filePath, unsigned __int64 bytesCompleted) 
        {
        }
        
        /*
        压缩/解压完成
        filePath : 目标路径
        */
        virtual void OnWorkEnd(const std::wstring& filePath)
        {
        }

        /*
        解压文件开始
        destFolder : 目标文件夹
        ItemPath   : 相对路径
        return     : 是否继续解压
        */
        virtual bool OnFileExtractBegin(const std::wstring& destFolder, std::wstring& ItemPath)
        { 
            return true; 
        }

        /*
        解压文件结束
        filePath       : 文件路径
        bytesCompleted : 文件尺寸
        return         : 是否继续解压
        */
        virtual bool OnFileExtractDone(const std::wstring& filePath, unsigned __int64 bytesCompleted) 
        { 
            return true; 
        }
        
        /*
        回滚文件进度
        filePath       : 文件路径
        return         : 是否继续回滚
        */
        virtual void OnFileExtractRollBack(const std::wstring& filePath)
        {
        }

    };
}
