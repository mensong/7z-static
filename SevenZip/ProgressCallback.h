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
        ��ʼ: ѹ��/��ѹ
        totalSize : ��ѹ���ļ��ܴ�С  /   ѹ������С? or ����ѹ�ļ��ܴ�С?
        */
        virtual void OnWorkStart(const std::wstring& filePath, unsigned __int64 totalSize) 
        {
        }

        /*
        ѹ��/��ѹÿһ��ѹ������
        */
        virtual bool OnWorkItemInfo(const SevenZip::FilePathInfo& itemInfo)
        {
            return true;
        }

        /*
        ��ֵ���� : ѹ��/��ѹ
        inSize   : �Ѵ����ļ��ܴ�С  / �Ѵ���ѹ������С
        outSize  : ����ѹ������С    / ��ѹ�����ļ��ܴ�С
        */
        virtual void OnWorkRadio(const std::wstring& filePath, unsigned __int64  inSize, unsigned __int64 outSize)
        {
        }

        /*
        �ļ����� : ѹ��/��ѹ
        filePath       : �ļ�
        bytesCompleted : �Ѵ����С
        */
        virtual void OnWorkProgress(const std::wstring& filePath, unsigned __int64 bytesCompleted) 
        {
        }
        
        /*
        ѹ��/��ѹ���
        filePath : Ŀ��·��
        */
        virtual void OnWorkEnd(const std::wstring& filePath)
        {
        }

        /*
        ��ѹ�ļ���ʼ
        destFolder : Ŀ���ļ���
        ItemPath   : ���·��
        return     : �Ƿ������ѹ
        */
        virtual bool OnFileExtractBegin(const std::wstring& destFolder, std::wstring& ItemPath)
        { 
            return true; 
        }

        /*
        ��ѹ�ļ�����
        filePath       : �ļ�·��
        bytesCompleted : �ļ��ߴ�
        return         : �Ƿ������ѹ
        */
        virtual bool OnFileExtractDone(const std::wstring& filePath, unsigned __int64 bytesCompleted) 
        { 
            return true; 
        }
        
        /*
        �ع��ļ�����
        filePath       : �ļ�·��
        return         : �Ƿ�����ع�
        */
        virtual void OnFileExtractRollBack(const std::wstring& filePath)
        {
        }

    };
}
