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
#include <cerrno>
#include <map>

#include <cupt/common.hpp>

namespace cupt {
namespace internal {

void chomp(string& str)
{
	if (!str.empty() && *str.rbegin() == '\n') // the last character is newline
	{
		str.erase(str.end() - 1); // delete it
	}
}

vector< string > split(char c, const string& str, bool allowEmpty)
{
	vector< string > result;

	size_t size = str.size();
	size_t startPosition = 0;
	for (size_t i = 0; i < size; ++i)
	{
		if (str[i] == c)
		{
			if (startPosition < i || allowEmpty)
			{
				// there is non-empty substring (or empty one allowed)
				result.push_back(string(str, startPosition, i - startPosition));
			}
			startPosition = i + 1;
		}
	}
	if (startPosition < size || allowEmpty)
	{
		// there is non-empty last substring (or empty allowed)
		result.push_back(string(str, startPosition, size - startPosition));
	}

	return result;
}

string getWaitStatusDescription(int status)
{
	if (status == 0)
	{
		return "success";
	}
	else if (WIFSIGNALED(status))
	{
		return format2("terminated by signal '%s'", strsignal(WTERMSIG(status)));
	}
	else if (WIFSTOPPED(status))
	{
		return format2("stopped by signal '%s'", strsignal(WSTOPSIG(status)));
	}
	else if (WIFEXITED(status))
	{
		return format2("exit code '%d'", WEXITSTATUS(status));
	}
	else
	{
		return "unknown status";
	}
}

bool architectureMatch(const string& architecture, const string& pattern)
{
	static std::map< pair< string, string >, bool > cache;

	auto key = make_pair(architecture, pattern);
	auto insertResult = cache.insert(make_pair(key, false /* doesn't matter */));
	auto it = insertResult.first;
	if (insertResult.second)
	{
		// new element
		it->second = !system(format2("dpkg-architecture -a%s -i%s",
					architecture, pattern).c_str());
	}
	return it->second;
}

uint32_t string2uint32(pair< string::const_iterator, string::const_iterator > input)
{
	char buf[16] = {0};
	size_t inputLength = input.second - input.first;
	if (inputLength >= sizeof(buf))
	{
		fatal2(__("too long number string"));
	}
	memcpy(buf, &(*input.first), inputLength);
	errno = 0;
	long long number = strtoll(buf, NULL, 10);
	if (errno)
	{
		fatal2e(__("invalid number '%s'"), buf);
	}
	if (number < 0)
	{
		fatal2(__("negative number '%s'"), buf);
	}
	if (number >= 0x100000000LL) // uint32_t upper limit
	{
		fatal2(__("too big number '%s'"), buf);
	}
	return uint32_t(number);
}

template < class IterType, char symbol >
inline bool __find_space_symbol_space(const IterType& begin, const IterType& end,
		IterType& resultBegin, IterType& resultEnd)
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

template< class IterType, char symbol >
inline void __process_space_symbol_space_delimited_strings(IterType begin, IterType end,
		const std::function< void (IterType, IterType) >& callback)
{
	IterType current = begin;
	IterType delimiterBegin;
	IterType delimiterEnd;
	while (__find_space_symbol_space< IterType, symbol >(current, end, delimiterBegin, delimiterEnd))
	{
		callback(current, delimiterBegin);
		current = delimiterEnd;
	}
	callback(current, end);
}

void processSpaceCommaSpaceDelimitedStrings(const char* begin, const char* end,
		const std::function< void (const char*, const char*) >& callback)
{
	__process_space_symbol_space_delimited_strings<const char*, ','>(begin, end, callback);
}

void processSpaceCommaSpaceDelimitedStrings(string::const_iterator begin, string::const_iterator end,
		const std::function< void (string::const_iterator, string::const_iterator) >& callback)
{
	__process_space_symbol_space_delimited_strings<string::const_iterator, ','>(begin, end, callback);
}

void processSpacePipeSpaceDelimitedStrings(string::const_iterator begin, string::const_iterator end,
		const std::function< void (string::const_iterator, string::const_iterator) >& callback)
{
	__process_space_symbol_space_delimited_strings<string::const_iterator, '|'>(begin, end, callback);
}

} // namespace
} // namespace

