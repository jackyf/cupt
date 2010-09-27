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

#include <cupt/hashsums.hpp>
#include <cupt/file.hpp>

namespace cupt {

namespace {

string __get_hash(const HashSums::Type& type, const string& prefix, const string& postfix,
		const string& description)
{
	static const char* verifiers[] = { "md5sum", "sha1sum", "sha256sum" };

	string errorString;
	File pipe(sf("%s%s -b %s", prefix.c_str(), verifiers[type], postfix.c_str()), "pr", errorString);
	if (!errorString.empty())
	{
		warn("failed open hashsummer ('%s') pipe on %s: %s", verifiers[type],
				description.c_str(), errorString.c_str());
		return "";
	}

	string line;
	pipe.getLine(line);

	auto firstSpacePosition = line.find(' ');
	if (firstSpacePosition == string::npos)
	{
		warn("unexpected result while fetching %s hash sum from result '%s' on %s",
				verifiers[type], line.c_str(), description.c_str());
		return "";
	}

	return line.substr(0, firstSpacePosition); // trimming
}

void __assert_not_empty(const HashSums* hashSums)
{
	if (hashSums->empty())
	{
		fatal("no hash sums specified");
	}
}

}

bool HashSums::empty() const
{
	for (size_t type = 0; type < Count; ++type)
	{
		if (!values[type].empty())
		{
			return false;
		}
	}
	return true;
}

string& HashSums::operator[](const Type& type)
{
	return values[type];
}

const string& HashSums::operator[](const Type& type) const
{
	return values[type];
}

bool HashSums::verify(const string& path) const
{
	__assert_not_empty(this);

	size_t sumsCount = 0;

	for (size_t type = 0; type < Count; ++type)
	{
		if (values[type].empty())
		{
			// skip
			continue;
		}

		++sumsCount;

		string fileHashSum = __get_hash(static_cast<Type>(type), "",
				path.c_str(), sf("file '%s'", path.c_str()));

		if (fileHashSum != values[type])
		{
			// wrong hash sum
			return false;
		}
	}

	return true;
}

void HashSums::fill(const string& path)
{
	for (size_t type = 0; type < Count; ++type)
	{
		values[type]= __get_hash(static_cast<Type>(type), "",
				path.c_str(), sf("file '%s'", path.c_str()));
	}
}

bool HashSums::match(const HashSums& other) const
{
	__assert_not_empty(this);
	__assert_not_empty(&other);

	size_t comparesCount = 0;

	for (size_t i = 0; i < Count; ++i)
	{
		if (values[i].empty() || other.values[i].empty())
		{
			continue;
		}

		++comparesCount;
		if (values[i] != other.values[i])
		{
			return false;
		}
	}

	return comparesCount;
}

string HashSums::getStringHash(const Type& type, const string& pattern)
{
	string description = sf("string '%s'", pattern.c_str());
	string printfString;
	char hexBuffer[5] = {'\\', 'x', '\0'};
	FORIT(charIt, pattern)
	{
		unsigned int c = *charIt;
		sprintf(hexBuffer+2, "%02x", c);
		printfString.append(hexBuffer, 4);
	}
	return __get_hash(type, sf("/usr/bin/printf '%s' | ", printfString.c_str()), "", description);
}

}

