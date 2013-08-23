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

#include <internal/regex.hpp>

namespace cupt {
namespace internal {

vector< string > split(const sregex& regex, const string& str)
{
	vector< string > result;
	sregex_token_iterator tokenIterator(str.begin(), str.end(), regex, -1);
	sregex_token_iterator end;
	std::copy(tokenIterator, end, std::back_inserter(result));
	return result;
}

sregex stringToRegex(const string& input)
{
	try
	{
		return sregex::compile(input);
	}
	catch (regex_error& e)
	{
		fatal2(__("invalid regular expression '%s'"), input);
	}
}
sregex globToRegex(const string& glob)
{
	return stringToRegex(globToRegexString(glob));
}

}
}

