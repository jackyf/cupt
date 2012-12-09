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
#include <ctime>

#include <cupt/config.hpp>
#include <cupt/file.hpp>

#include <internal/logger.hpp>

namespace cupt {
namespace internal {

const char* Logger::__subsystem_strings[4] = {
	"session", "metadata", "packages", "snapshots"
};

Logger::Logger(const Config& config)
	: __file(NULL)
{
	__enabled = config.getBool("cupt::worker::log");
	__simulating = config.getBool("cupt::worker::simulate");
	__debugging = config.getBool("debug::logger");

	if (!__enabled)
	{
		return;
	}

	__levels[(int)Subsystem::Session] = 1;
	#define SET_LEVEL(x) __levels[x] = config.getInteger( \
			string("cupt::worker::log::levels::") + __subsystem_strings[x]);
	SET_LEVEL(int(Subsystem::Metadata))
	SET_LEVEL(int(Subsystem::Packages))
	SET_LEVEL(int(Subsystem::Snapshots))
	#undef SET_LEVEL

	if (!__simulating)
	{
		auto path = config.getPath("cupt::directory::log");
		__file = new RequiredFile(path, "a");
	}

	log(Subsystem::Session, 1, "started");
}

Logger::~Logger()
{
	log(Subsystem::Session, 1, "finished");
	delete __file;
}

string Logger::__get_log_string(Subsystem subsystem, Level level, const string& message)
{
	char dateTime[10 + 1 + 8 + 1]; // "2011-05-20 12:34:56"
	{
		struct tm tm;
		auto timestamp = time(NULL);
		localtime_r(&timestamp, &tm);
		strftime(dateTime, sizeof(dateTime), "%F %T", &tm);
	}
	string indent((level - 1) * 2, ' ');
	return string(dateTime) + " | " + __subsystem_strings[(int)subsystem] +
			": " + indent + message;
}

void Logger::log(Subsystem subsystem, Level level, const string& message, bool force)
{
	if (level == 0)
	{
		fatal2i("logger: log: level should be >= 1");
	}

	if (!__enabled)
	{
		return;
	}

	if (__debugging)
	{
		debug2("log: %s", __get_log_string(subsystem, level, message));
	}

	if (!__simulating)
	{
		if (force || (level <= __levels[(int)subsystem]))
		{
			auto logData = __get_log_string(subsystem, level, message) + "\n";
			// stdio-buffered fails when there are several writing processes
			__file->unbufferedPut(logData.c_str(), logData.size());
		}
	}
}


}
}
