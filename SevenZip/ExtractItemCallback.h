#pragma once
#include "ExtractItemInfo.h"

namespace SevenZip
{
    class ExtractItemCallback
    {
    public:
        virtual void OnStart(const SevenZip::TString& archivePath, unsigned int itemCount){  }

        virtual void OnItem(SevenZip::ExtractItemInfo* itemInfo){  }
        
        virtual void OnEnd() {}

    };
}
