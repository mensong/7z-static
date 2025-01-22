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
        virtual void OnStart(const std::wstring& filePath, unsigned __int64 totalSize) 
        {
        }

        /*
        * ����ÿһ��ѹ������
        */
        virtual bool OnItem(const SevenZip::FilePathInfo& itemInfo)
        {
            return true;
        }

        /*
        ��ֵ���� : ѹ��/��ѹ
        inSize   : �Ѵ����ļ��ܴ�С  / �Ѵ���ѹ������С
        outSize  : ����ѹ������С    / ��ѹ�����ļ��ܴ�С
        */
        virtual void OnRadio(const std::wstring& filePath, unsigned __int64  inSize, unsigned __int64 outSize)
        {
        }

        /*
        �ļ����� : ѹ��/��ѹ
        filePath       : �ļ�
        bytesCompleted : �Ѵ����С
        */
        virtual void OnProgress(const std::wstring& filePath, unsigned __int64 bytesCompleted) 
        {
        }
        
        /*
        ѹ��/��ѹ���
        filePath : Ŀ��·��
        */
        virtual void OnEnd(const std::wstring& filePath)
        {
        }

        /*
        ��ѹ�ļ���ʼ
        destFolder : Ŀ���ļ���
        ItemPath   : ���·��
        return     : �Ƿ������ѹ
        */
        virtual bool OnFileBegin(const std::wstring& destFolder, std::wstring& ItemPath)
        { 
            return true; 
        }

        /*
        ��ѹ�ļ�����
        filePath       : �ļ�·��
        bytesCompleted : �ļ��ߴ�
        return         : �Ƿ������ѹ
        */
        virtual bool OnFileDone(const std::wstring& filePath, unsigned __int64 bytesCompleted) 
        { 
            return true; 
        }
        
        /*
        �ع��ļ�����
        filePath       : �ļ�·��
        return         : �Ƿ�����ع�
        */
        virtual void OnRollBack(const std::wstring& filePath)
        {
        }

    };
}
