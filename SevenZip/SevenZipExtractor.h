#pragma once
#include "SevenZipArchive.h"
#include "CompressionFormat.h"
#include "ProgressCallback.h"
#include "SevenZipPwd.h"
#include "ExtractItemCallback.h"

namespace SevenZip
{

    class SevenZipExtractor : public SevenZipArchive
    {
    public: 
        SevenZipExtractor();
        virtual ~SevenZipExtractor();

		virtual HRESULT ExtractArchive(
            const TString& directory, 
            ProgressCallback* callback, 
            ExtractItemCallback* itemCallback,
            SevenZipPassword *pSevenZipPassword=NULL,
            bool testMode = false);
        const TString& GetErrorString();

        void SetOverwriteMode(const OverwriteModeEnum& mode);
        OverwriteModeEnum GetOverwriteMode();

    private:

		HRESULT ExtractArchive(
            const CMyComPtr< IStream >& archiveStream, 
            const TString& directory, 
            ProgressCallback* callback, 
            ExtractItemCallback* itemCallback, 
            bool testMode,
            SevenZipPassword *pSevenZipPassword);

        OverwriteModeEnum m_overwriteMode;
        TString m_message;
    };
}
