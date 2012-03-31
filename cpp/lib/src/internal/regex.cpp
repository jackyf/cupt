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

string globToRegexString(const string& input)
{
	// quoting all metacharacters
	static const sregex metaCharRegex = sregex::compile("[^A-Za-z0-9_]");
	string output = regex_replace(input, metaCharRegex, "\\$&");
	static const sregex questionSignRegex = sregex::compile("\\\\\\?");
	output = regex_replace(output, questionSignRegex, ".");
	static const sregex starSignRegex = sregex::compile("\\\\\\*");
	output = regex_replace(output, starSignRegex, ".*?");

	return string("^") + output + "$";
}

shared_ptr< sregex > stringToRegex(const string& input)
{
	shared_ptr< sregex > result;
	try
	{
		result = shared_ptr< sregex >(new sregex(sregex::compile(input)));
	}
	catch (regex_error& e)
	{
		fatal2(__("invalid regular expression '%s'"), input);
	}
	return result;
}
shared_ptr< sregex > globToRegex(const string& glob)
{
	return stringToRegex(globToRegexString(glob));
}

}
}

