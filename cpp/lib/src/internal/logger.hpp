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
#ifndef CUPT_INTERNAL_LOGGER_SEEN
#define CUPT_INTERNAL_LOGGER_SEEN

#include <cupt/fwd.hpp>

namespace cupt {
namespace internal {

class Logger
{
 public:
	typedef uint16_t Level;
	enum class Subsystem { Session, Metadata, Packages, Snapshots };

	Logger(const Config& config);
	~Logger();
	void log(Subsystem, Level, const string& message, bool force = false);

	template< typename... Args >
	void loggedFatal2(Subsystem subsystem, Level level,
			string (*formatFunc)(const string&, const Args&...),
			const string& format, const Args&... args)
	{
		this->log(subsystem, level,
				string("error: ") + formatFunc(format, args...), true);
		fatal2("%s", formatFunc(__(format.c_str()), args...));
	}
 private:
	Level __levels[4];
	File* __file;
	bool __enabled;
	bool __simulating;
	bool __debugging;

	string __get_log_string(Subsystem, Level, const string& message);

	static const char* __subsystem_strings[4];
};

}
}

#endif

