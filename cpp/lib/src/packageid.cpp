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
#include <cstddef>
#include <unordered_map>

#include <cupt/common.hpp>

namespace cupt {

namespace {

class StringBuffer
{
	union
	{
		const char* __bufferStart;
		const string* __string;
	};
	size_t __bufferLength;

	bool __containsString() const { return __bufferLength == size_t(-1); }

 public:
	StringBuffer(const char* start, size_t len)
		: __bufferStart(start), __bufferLength(len)
	{}
	StringBuffer(const StringBuffer& other)
		: __string(new string(other.__bufferStart, other.__bufferLength)), __bufferLength(size_t(-1))
	{}
	StringBuffer(StringBuffer&& other)
		: __bufferStart(other.__bufferStart), __bufferLength(other.__bufferLength)
	{
		other.__bufferLength = 0; // any non-(-1)
	}
	~StringBuffer()
	{
		if (__containsString()) delete __string;
	}
	const char* getBufferStart() const
	{
		return __containsString() ? __string->data() : __bufferStart;
	}
	size_t getBufferLength() const
	{
		return __containsString() ? __string->size() : __bufferLength;
	}
	bool operator==(const StringBuffer& other) const
	{
		auto ourLength = getBufferLength();
		if (ourLength != other.getBufferLength()) return false;
		return memcmp(getBufferStart(), other.getBufferStart(), ourLength) == 0;
	}
	const string* getStringPtr() const
	{
		return __string;
	}
};
struct StringBufferHasher
{
	size_t operator()(const StringBuffer& sb) const
	{
		auto buf = reinterpret_cast< const unsigned char* >(sb.getBufferStart());
		auto end = buf + sb.getBufferLength();

		size_t h = 0;
		while (buf != end)
		{
			h = h*31 + *(buf++);
		}

		return h;
	}
};

vector< const string* >& getN2S()
{
	static vector< const string* > n2s;
	return n2s;
}

uint32_t getPackageNameId(StringBuffer&& packageName)
{
	if (!PackageId::checkPackageName(packageName.getBufferStart(), packageName.getBufferLength()))
	{
		fatal2(__("invalid package name '%s'"), *StringBuffer(packageName).getStringPtr());
	}

	typedef std::unordered_map< StringBuffer, uint32_t, StringBufferHasher > S2NType;

	static S2NType s2n;
	static uint32_t nextId = 1;

	S2NType::value_type insertPair { std::move(packageName), 0 };
	auto& insertedPair = *s2n.insert(insertPair).first;
	auto& id = insertedPair.second;
	if (!id)
	{
		id = nextId++;
		getN2S().push_back(insertedPair.first.getStringPtr());
	}

	return id;
}

}

PackageId::PackageId()
	: __id(0)
{}

PackageId::PackageId(const char* start, size_t len)
	: __id(getPackageNameId(StringBuffer(start, len)))
{}

PackageId::PackageId(const string& packageName)
	: __id(getPackageNameId(StringBuffer(packageName.data(), packageName.size())))
{}

uint32_t PackageId::rawId() const
{
	return __id;
}

const string& PackageId::name() const
{
	return *(getN2S()[__id-1]);
}

bool PackageId::checkPackageName(const char* buffer, size_t length)
{
	const char* const inputStart = buffer;
	const char* const inputEnd = buffer + length;
	const char* resultEnd;
	consumePackageName(inputStart, inputEnd, resultEnd);
	return (resultEnd == inputEnd);
}

}

