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
#include <cupt/versionstring.hpp>

namespace cupt {
namespace versionstring {

char idSuffixDelimiter = '^';

//TODO: VersionString class?
string getOriginal(const string& s)
{
	return s.substr(0, s.rfind(idSuffixDelimiter));
}

bool sameOriginal(const string& left, const string& right)
{
	return left.compare(0, left.rfind(idSuffixDelimiter),
			right, 0, right.rfind(idSuffixDelimiter)) == 0;
}

}
}
