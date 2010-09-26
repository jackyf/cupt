/**************************************************************************
*   Copyright (C) 2010 by Eugene V. Lyubimkin                             *
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License                  *
*   (version 3 or above) as published by the Free Software Foundation.    *
*                                                                         *
*   This program is distributed in the hope that it will be useful,       *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*   GNU General Public License for more details.                          *
*                                                                         *
*   You should have received a copy of the GNU GPL                        *
*   along with this program; if not, write to the                         *
*   Free Software Foundation, Inc.,                                       *
*   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA               *
**************************************************************************/

#include <internal/horspoolsearcher.hpp>

namespace cupt {
namespace internal {

HorspoolSearcher::HorspoolSearcher(const string& pattern)
{
	// preprocessing pattern
	auto patternSize = pattern.size();
	if (patternSize > 255)
	{
		fatal("internal error: horspool searcher: received pattern with length more than 255 bytes");
	}
	memset(__shifts, int(patternSize), sizeof(__shifts));
	for (size_t i = 0; i < patternSize - 1; ++i) // - 1: don't touch last character
	{
		__shifts[(uint8_t)pattern[i]] = patternSize - i - 1;
	}
	__pattern = pattern;
}

size_t HorspoolSearcher::searchIn(const string& text) const
{
	const size_t patternSize = __pattern.size();
	const size_t textSize = text.size();
	const char lastCharacter = *(__pattern.rbegin());
	const size_t maxOffset = textSize - patternSize + 1;

	for (size_t i = 0; i < maxOffset;)
	{
		char candidateLastCharacter = text[i + patternSize - 1];
		if (lastCharacter == candidateLastCharacter)
		{
			if (!memcmp(&text[i], &__pattern[0], patternSize-1))
			{
				return i;
			}
		}
		i += __shifts[(uint8_t)candidateLastCharacter];
	}

	return string::npos;
}

}
}

