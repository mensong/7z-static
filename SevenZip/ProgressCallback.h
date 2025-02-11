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
        virtual void OnStart(const std::wstring& filePath, unsigned __int64 totalSize) 
        {
        }

        /*
        * 处理每一个压缩对象
        */
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
        filePath : 目标路径
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
}
