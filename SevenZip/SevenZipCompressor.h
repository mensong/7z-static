#pragma once
#include <vector>
#include "SevenZipArchive.h"
#include "FileInfo.h"
#include "CompressionFormat.h"
#include "CompressionLevel.h"
#include "ProgressCallback.h"
#include "SevenZipPwd.h"

namespace SevenZip
{
	class SevenZipCompressor : public SevenZipArchive
	{
	public:

		SevenZipCompressor();
		virtual ~SevenZipCompressor();

		// Includes the last directory as the root in the archive, e.g. specifying "C:\Temp\MyFolder"
		// makes "MyFolder" the single root item in archive with the files within it included.
		virtual HRESULT CompressDirectoryWithRoot(
			const TString& directory, 
			const TString& searchFilter = L"*", 
			ProgressCallback* callback = NULL, 
			bool includeSubdirs = true, 
			SevenZipPassword *pSevenZipPassword = NULL);

		// Excludes the last directory as the root in the archive, its contents are at root instead. E.g.
		// specifying "C:\Temp\MyFolder" make the files in "MyFolder" the root items in the archive.
		virtual HRESULT CompressDirectory(
			const TString& directory, 
			const TString& searchFilter = L"*", 
			ProgressCallback* callback = NULL, 
			bool includeSubdirs = true, 
			SevenZipPassword* pSevenZipPassword = NULL);

		// Compress just this single file as the root item in the archive.
		virtual HRESULT CompressFile(
			const TString& filePath, 
			ProgressCallback* callback = NULL, 
			SevenZipPassword *pSevenZipPassword=NULL);

		//Compress files to archive
        virtual HRESULT CompressFiles(
			const TString& pathPrefix,
			const std::vector<TString>& files,
			ProgressCallback* callback = NULL,
			SevenZipPassword *pSevenZipPassword = NULL);

	private:
		//the final compression result compression path. Used for tracking in callbacks
		TString m_srcPath;
		CMyComPtr< IStream > OpenArchiveStream();
        HRESULT FindAndCompressFiles(const TString& directory, const TString& searchPattern,
			const TString& pathPrefix, bool recursion, ProgressCallback* callback, SevenZipPassword *pSevenZipPassword);
		HRESULT CompressFilesToArchive(const TString& pathPrefix, const std::vector< FilePathInfo >& filePaths, ProgressCallback* callback, SevenZipPassword *pSevenZipPassword);
        HRESULT SetCompressionProperties(IUnknown* outArchive);
	};
}
