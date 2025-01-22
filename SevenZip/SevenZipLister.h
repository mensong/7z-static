#pragma once
#include "SevenZipArchive.h"
#include "CompressionFormat.h"
#include "ListCallback.h"
#include "SevenZipPwd.h"

namespace SevenZip
{
	class SevenZipLister : public SevenZipArchive
	{
	public:

		SevenZipLister();
		virtual ~SevenZipLister();

		virtual HRESULT ListArchive(ListCallback* callback, SevenZipPassword* pSevenZipPassword = NULL);

	private:
		HRESULT ListArchive(const CMyComPtr< IStream >& archiveStream, ListCallback* callback, SevenZipPassword* pSevenZipPassword);
	};
}
