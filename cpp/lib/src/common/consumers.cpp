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
#include <cupt/common.hpp>

namespace cupt {

void consumePackageName(string::const_iterator begin, string::const_iterator end,
		string::const_iterator& resultEnd)
{
	// "[a-z_0-9.+-]+"
	resultEnd = begin; // start position, meaning no package name found
	while (resultEnd != end && (
			(*resultEnd >= 'a' && *resultEnd <= 'z') ||
			*resultEnd == '_' ||
			(*resultEnd >= '0' && *resultEnd <= '9') ||
			*resultEnd == '.' || *resultEnd == '+' || *resultEnd == '-')
		  )
	{
		++resultEnd;
	}
}

bool checkPackageName(const string& input, bool throwOnError)
{
	string::const_iterator resultEnd;
	consumePackageName(input.begin(), input.end(), resultEnd);
	bool result = (resultEnd == input.end());
	if (!result && throwOnError)
	{
		fatal2(__("invalid package name '%s'"), input);
	}
	return result;
}

// kind of HACK: I want to use this function, but don't want to create a header for it
typedef pair< string::const_iterator, string::const_iterator > StringAnchorPair;
void __divide_versions_parts(const string& versionString,
		StringAnchorPair& epoch, StringAnchorPair& upstream, StringAnchorPair& revision);

inline bool __check_version_symbol(char symbol)
{
	return (symbol >= 'a' && symbol <= 'z') ||
			(symbol >= 'A' && symbol <= 'Z') ||
			symbol == '+' ||
			(symbol >= '0' && symbol <= '9') ||
			(symbol == '.' || symbol == '~' || symbol == '-');
}

bool __check_version_string(const string& input,
		bool& underscoresPresent, char& firstUpstreamCharacter)
{
	underscoresPresent = false;
	if (input.empty() || input[0] == ':' /* empty epoch */ || *(input.rbegin()) == '-' /* empty revision */)
	{
		return false;
	}

	// three parts:
	// 1: non-mandatory epoch (numbers, following ':')
	// 2: upstream part ([a-zA-Z+0-9~.-] and semicolon (if epoch exists))
	// 3: non-mandatory debian revision (starting '-', following [a-zA-Z+0-9~_.]+)
	StringAnchorPair epoch, upstream, revision;
	__divide_versions_parts(input, epoch, upstream, revision);

	// checking epoch
	string::const_iterator current = epoch.first;
	while (current != epoch.second)
	{
		if (*current < '0' || *current > '9')
		{
			return false;
		}
		++current;
	}

	// checking upstream version
	bool colonAllowed = (epoch.first != epoch.second);
	if (upstream.first == upstream.second)
	{
		return false; // should be non-empty
	}
	current = upstream.first;
	firstUpstreamCharacter = *current;
	while (current != upstream.second)
	{
		if (!(__check_version_symbol(*current) || (colonAllowed && *current == ':')))
		{
			return false;
		}
		if (*current == '_')
		{
			underscoresPresent = true;
		}
		++current;
	}

	// checking debian revision
	current = revision.first;
	while (current != revision.second)
	{
		if (!__check_version_symbol(*current))
		{
			return false;
		}
		if (*current == '_')
		{
			underscoresPresent = true;
		}
		++current;
	}

	return true;
}

bool checkVersionString(const string& input, bool throwOnError)
{
	bool underscoresPresent;
	char firstUpstreamCharacter;
	bool result = __check_version_string(input, underscoresPresent, firstUpstreamCharacter);
	if (!result && throwOnError)
	{
		fatal2(__("invalid version string '%s'"), input);
	}
	if (result)
	{
		if (underscoresPresent)
		{
			warn2(__("version string '%s': should not contain underscores"), input);
		}
		if (firstUpstreamCharacter < '0' || firstUpstreamCharacter > '9')
		{
			warn2(__("version string '%s': first upstream character '%c' is not a digit"),
					input, firstUpstreamCharacter);
		}
	}
	return result;
}

}

