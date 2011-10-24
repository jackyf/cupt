/**************************************************************************
*   Copyright (C) 2011 by Eugene V. Lyubimkin                             *
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

#include <common/format2.hpp>

namespace cupt {

CUPT_API void __mwrite_line(const char*, const string&);

template < typename... Args >
void fatal2(const char* format, const Args&... args)
{
	auto errorString = format2(format, args...);
	__mwrite_line("E: ", errorString);
	throw Exception(errorString);
}

template < typename... Args >
void fatal2e(const char* format, const Args&... args)
{
	auto errorString = format2e(format, args...);
	__mwrite_line("E: ", errorString);
	throw Exception(errorString);
}

template < typename... Args >
void warn2(const char* format, const Args&... args)
{
	__mwrite_line("W: ", format2(format, args...));
}

template < typename... Args >
void warn2e(const char* format, const Args&... args)
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

