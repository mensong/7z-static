#pragma once
#include "Enum.h"

namespace SevenZip
{
	struct CompressionLevel
	{
		enum _Enum
		{
			Default = 0,
			Level_1 = 1,
			Level_2 = 2,
			Level_3 = 3,
			Level_4 = 4,
			Level_5 = 5,
			Level_6 = 6,
			Level_7 = 7,
			Level_8 = 8,
			Level_9 = 9,
		};
	
		typedef intl::EnumerationDefinitionNoStrings _Definition;
		typedef intl::EnumerationValue< _Enum, _Definition, Default > _Value;
	};
	
	typedef CompressionLevel::_Value CompressionLevelEnum;
}
