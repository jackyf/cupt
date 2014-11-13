/**************************************************************************
*   Copyright (C) 2013 by Eugene V. Lyubimkin                             *
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
#ifndef CUPT_RANGE_SEEN
#define CUPT_RANGE_SEEN

#include <utility>
#include <vector>

namespace cupt {

template < typename IteratorT >
struct Range: public std::pair< IteratorT, IteratorT >
{
	typedef std::pair< IteratorT, IteratorT > Base;
	Range(const IteratorT& from, const IteratorT& to)
		: Base(from, to)
	{}
	IteratorT begin() const
	{
		return Base::first;
	}
	IteratorT end() const
	{
		return Base::second;
	}
	typedef typename std::decay<decltype(*std::declval<IteratorT>())>::type Value;
	auto asVector() const -> std::vector< Value >
	{
		vector< Value > result;
		for (const auto& element: *this)
		{
			result.push_back(element);
		}
		return result;
	}

	typedef IteratorT Iterator;
};

}

#endif

