/**************************************************************************
*   Copyright (C) 2011-2012 by Eugene V. Lyubimkin                        *
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

// only for internal inclusion
/// @cond

#include <cstdio>
#include <cstring>

#include <string>

namespace cupt {

using std::string;

namespace internal {
namespace format2impl {

template < typename... All >
struct Tuple;

template < typename Head, typename... Tail >
struct Tuple< Head, Tail... >
{
	const Head& head;
	Tuple< Tail... > tail;

	Tuple< Head, Tail...>(const Head& head_, const Tail&... tail_)
		: head(head_), tail(tail_...)
	{}
};

template <>
struct Tuple<>
{};

template < typename... Args >
string tupleformat(Tuple<>, Args... args)
{
	char formattedBuffer[4096];

	auto bytesWritten = snprintf(formattedBuffer, sizeof(formattedBuffer), args...);

	if ((size_t)bytesWritten < sizeof(formattedBuffer))
	{
		return string(formattedBuffer);
	}
	else
	{
		// we need a bigger buffer, allocate it dynamically
		auto size = bytesWritten+1;
		char* dynamicBuffer = new char[size];
		snprintf(dynamicBuffer, size, args...);
		string result(dynamicBuffer);
		delete [] dynamicBuffer;
		return result;
	}
}

template< typename TupleT, typename... Args >
string tupleformat(TupleT&& tuple, Args... args)
{
	return tupleformat(tuple.tail, args..., tuple.head);
}

template < typename... TupleTailT, typename... Args >
string tupleformat(Tuple< string, TupleTailT... > tuple, Args... args)
{
	return tupleformat(tuple.tail, args..., tuple.head.c_str());
}

}
}

// now public parts

template < typename... Args >
string format2(const char* format, const Args&... args)
{
	return internal::format2impl::tupleformat(
			internal::format2impl::Tuple< const char*, Args... >(format, args...));
}

template < typename... Args >
string format2(const string& format, const Args&... args)
{
	return format2(format.c_str(), args...);
}

template < typename... Args >
string format2e(const char* format, const Args&... args)
{
	char errorBuffer[255] = "?";
	// error message may not go to errorBuffer, see man strerror_r (GNU version)
	auto errorString = strerror_r(errno, errorBuffer, sizeof(errorBuffer));

	return format2(format, args...) + ": " + errorString;
}

template < typename... Args >
string format2e(const string& format, const Args&... args)
{
	return format2e(format.c_str(), args...);
}

CUPT_API void __mwrite_line(const char*, const string&);

template < typename... Args >
void fatal2(const string& format, const Args&... args)
{
	auto errorString = format2(format, args...);
	__mwrite_line("E: ", errorString);
	throw Exception(errorString);
}

template < typename... Args >
void fatal2i(const char* format, const Args&... args)
{
	fatal2((string("internal error: ") + format), args...);
}

template < typename... Args >
void fatal2e(const string& format, const Args&... args)
{
	auto errorString = format2e(format, args...);
	__mwrite_line("E: ", errorString);
	throw Exception(errorString);
}

template < typename... Args >
void warn2(const string& format, const Args&... args)
{
	__mwrite_line("W: ", format2(format, args...));
}

template < typename... Args >
void warn2e(const string& format, const Args&... args)
{
	__mwrite_line("W: ", format2e(format, args...));
}

template < typename... Args >
void debug2(const char* format, const Args&... args)
{
	__mwrite_line("D: ", format2(format, args...));
}

template < typename... Args >
void simulate2(const char* format, const Args&... args)
{
	__mwrite_line("S: ", format2(format, args...));
}

}

/// @endcond

