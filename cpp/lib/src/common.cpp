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
#include <libintl.h>

#include <algorithm>
#include <cstdarg>
#include <cstring>

#include <cupt/common.hpp>

#include <internal/common.hpp>

namespace cupt {

#define QUOTED(x) QUOTED_(x)
#define QUOTED_(x) # x
const char* const libraryVersion = QUOTED(CUPT_VERSION);
#undef QUOTED
#undef QUOTED_

string __errno2string(int savedErrno)
{
	char errorBuffer[255] = { '?', '\0' };
	return string(strerror_r(savedErrno, errorBuffer, sizeof(errorBuffer)));
}

string __substitute_eee(const char* format, int savedErrno)
{
	// replacing EEE (if exist) with error string
	string result(format);
	size_t pos = result.find(string("EEE"));
	if (pos != string::npos)
	{
		result.replace(pos, 3, __errno2string(savedErrno));
	}
	return result;
}

string __get_formatted_string(const char* format, va_list va)
{
	char formattedBuffer[4096];

	va_list vb;
	va_copy(vb, va);

	auto substitutedFormat = __substitute_eee(format, errno);

	auto bytesWritten = vsnprintf(formattedBuffer, sizeof(formattedBuffer),
			substitutedFormat.c_str(), vb);

	va_end(vb);

	if ((size_t)bytesWritten < sizeof(formattedBuffer))
	{
		return string(formattedBuffer);
	}
	else
	{
		// we need a bigger buffer, allocate it dynamically
		auto size = bytesWritten+1;
		char* dynamicBuffer = new char[size];
		vsnprintf(dynamicBuffer, size, substitutedFormat.c_str(), va);
		string result(dynamicBuffer);
		delete [] dynamicBuffer;
		return result;
	}
}

int messageFd = -1;

inline void __mwrite(const string& output)
{
	if (messageFd != -1)
	{
		write(messageFd, output.c_str(), output.size());
	}
}

void __mwrite_line(const char* prefix, const string& message)
{
	__mwrite(string(prefix) + message + "\n");
}

void fatal(const char* format, ...)
{
	va_list va;
	va_start(va, format);
	auto errorString = __get_formatted_string(format, va);
	va_end(va);

	__mwrite_line("E: ", errorString);

	throw Exception(errorString);
}

void warn(const char* format, ...)
{
	va_list va;
	va_start(va, format);
	auto formattedString = __get_formatted_string(format, va);
	va_end(va);

	__mwrite_line("W: ", formattedString);
}

void debug(const char* format, ...)
{
	va_list va;
	va_start(va, format);
	auto formattedString = __get_formatted_string(format, va);
	va_end(va);

	__mwrite_line("D: ", formattedString);
}

void simulate(const char* format, ...)
{
	va_list va;
	va_start(va, format);
	auto formattedString = __get_formatted_string(format, va);
	va_end(va);

	__mwrite_line("S: ", formattedString);
}

string sf(const string& format, ...)
{
	va_list va;
	va_start(va, format);
	auto formattedString = __get_formatted_string(format.c_str(), va);
	va_end(va);

	return formattedString;
}

string join(const string& joiner, const vector< string >& parts)
{
	if (parts.empty())
	{
		return "";
	}
	string result = parts[0];
	auto size = parts.size();
	for (size_t i = 1; i < size; ++i)
	{
		result += joiner;
		result += parts[i];
	}
	return result;
}

string humanReadableSizeString(uint64_t bytes)
{
	char buf[32];
	if (bytes < 10*1000)
	{
		sprintf(buf, "%uB", (unsigned int)bytes);
	}
	else if (bytes < 100*1024)
	{
		sprintf(buf, "%.1fKiB", float(bytes) / 1024);
	}
	else if (bytes < 10*1000*1024)
	{
		sprintf(buf, "%.0fKiB", float(bytes) / 1024);
	}
	else if (bytes < 100*1024*1024)
	{
		sprintf(buf, "%.1fMiB", float(bytes) / 1024 / 1024);
	}
	else if (bytes < 10UL*1000*1024*1024)
	{
		sprintf(buf, "%.0fMiB", float(bytes) / 1024 / 1024);
	}
	else
	{
		sprintf(buf, "%.1fGiB", float(bytes) / 1024 / 1024 / 1024);
	}

	return string(buf);
}

const char* __(const char* buf)
{
	return dgettext("cupt", buf);
}

} // namespace

