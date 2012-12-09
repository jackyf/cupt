/**************************************************************************
*   Copyright (C) 2012 by Eugene V. Lyubimkin                             *
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

namespace cupt {
namespace internal {
namespace parse {

namespace {

template < class IterT >
inline bool findSpaceSymbolSpace(const IterT& begin, const IterT& end,
		char symbol, IterT& resultBegin, IterT& resultEnd)
{
	for (auto current = begin; current != end; ++current)
	{
		if (*current == symbol)
		{
			// found!
			resultBegin = current;
			while (resultBegin != begin && *(resultBegin-1) == ' ')
			{
				--resultBegin;
			}
			resultEnd = current+1;
			while (resultEnd != end && *resultEnd == ' ')
			{
				++resultEnd;
			}
			return true;
		}
	}
	return false;
}

}

template< typename IterT, typename CallbackT >
void processSpaceCharSpaceDelimitedStrings(IterT begin, IterT end,
		char delimiter, const CallbackT& callback)
{
	IterT current = begin;
	IterT delimiterBegin;
	IterT delimiterEnd;
	while (findSpaceSymbolSpace(current, end, delimiter, delimiterBegin, delimiterEnd))
	{
		callback(current, delimiterBegin);
		current = delimiterEnd;
	}
	callback(current, end);
}

}
}
}

