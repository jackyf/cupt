/**************************************************************************
*   Copyright (C) 2014 by Eugene V. Lyubimkin                             *
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
#ifndef CUPT_STRINGRANGE_SEEN
#define CUPT_STRINGRANGE_SEEN

#include <cupt/range.hpp>

#include <algorithm>

namespace cupt {

struct StringRange: public Range<const char*>
{
	typedef Range<const char*> Base;

	StringRange(const string& s)
		: Base(&*s.begin(), &*s.end())
	{}

	StringRange(Iterator a, Iterator b)
		: Base(a, b)
	{}

	string toStdString() const
	{
		return string(begin(), end());
	}

	Iterator find(char c) const
	{
		return std::find(begin(), end(), c);
	}
};

}

#endif

