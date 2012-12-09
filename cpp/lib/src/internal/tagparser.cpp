/**************************************************************************
*   Copyright (C) 2010-2011 by Eugene V. Lyubimkin                        *
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
#include <cstring>
#include <cctype>

#include <cupt/file.hpp>

#include <internal/tagparser.hpp>

namespace cupt {
namespace internal {

TagParser::TagParser(File* input)
	: __input(input), __buffer(NULL)
{}

bool TagParser::parseNextLine(StringRange& tagName, StringRange& tagValue)
{
	if (!__buffer)
	{
		__input->rawGetLine(__buffer, __buffer_size);
	}

	do
	{
		if (__buffer_size < 2)
		{
			__buffer = NULL;
			return false;
		}
		// if line starts with a blank character, get new line and restart the loop
	} while (isblank(__buffer[0]) && (__input->rawGetLine(__buffer, __buffer_size), true));

	{ // ok, first line is ready
		// chopping last '\n' if present
		if (__buffer[__buffer_size-1] == '\n')
		{
			--__buffer_size;
		}
		// get tag name
		auto colonPosition = memchr(__buffer+1, ':', __buffer_size - 1); // can't be very first
		if (!colonPosition)
		{
			fatal2(__("didn't find a colon in the line '%s'"), string(__buffer, __buffer_size));
		}
		tagName.first = decltype(tagName.first)(__buffer);
		tagName.second = decltype(tagName.second)((const char*)colonPosition);
		// getting tag value on a first line
		tagValue.first = decltype(tagValue.first)((const char*)colonPosition+1);
		if (isblank(*tagValue.first))
		{
			++tagValue.first;
		}
		tagValue.second = decltype(tagValue.second)(__buffer + __buffer_size);
	}
	__buffer = NULL;
	return true;
}

void TagParser::parseAdditionalLines(string& lines)
{
	// now let's see if there are any additional lines for the tag
	while (__input->rawGetLine(__buffer, __buffer_size), (__buffer_size > 1 && isblank(__buffer[0])))
	{
		lines.append(__buffer, __buffer_size);
	}
}

}
}

