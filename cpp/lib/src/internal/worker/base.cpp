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
#include <cupt/config.hpp>
#include <cupt/file.hpp>

#include <internal/lock.hpp>
#include <internal/common.hpp>

#include <internal/worker/base.hpp>

namespace cupt {
namespace internal {

WorkerBase::WorkerBase()
{
	fatal2i("WorkerBase::WorkerBase shouldn't be ever called");
}

WorkerBase::WorkerBase(const shared_ptr< const Config >& config, const shared_ptr< const Cache >& cache)
	: _config(config), _cache(cache)
{
	_logger = new Logger(*_config);

	__umask = umask(0022);

	string lockPath = _config->getPath("cupt::directory::state") + "/lock";
	__lock = new Lock(*_config, lockPath);
}

WorkerBase::~WorkerBase()
{
	delete __lock;
	umask(__umask);
	delete _logger;
}

string WorkerBase::_get_archives_directory() const
{
	return _config->getPath("dir::cache::archives");
}

string WorkerBase::_get_archive_basename(const BinaryVersion* version)
{
	return version->packageName + '_' + version->versionString + '_' +
			version->architecture + ".deb";
}

void WorkerBase::_run_external_command(Logger::Subsystem subsystem,
		const string& command, const string& commandInput, const string& errorId)
{
	const Logger::Level level = 3;
	_logger->log(subsystem, level, format2("running: %s", command));

	if (_config->getBool("cupt::worker::simulate"))
	{
		if (commandInput.empty())
		{
			simulate2("running command '%s'", command);
		}
		else
		{
			simulate2("running command '%s' with the input\n-8<-\n%s\n->8-",
					command, commandInput);
		}
	}
	else
	{
		const string& id = (errorId.empty() ? command : errorId);

		if (commandInput.empty())
		{
			// invoking command
			auto result = ::system(command.c_str());
			if (result == -1)
			{
				_logger->loggedFatal2(subsystem, level,
						format2e, "unable to launch the command '%s'", command);
			}
			else if (result)
			{
				_logger->loggedFatal2(subsystem, level,
						format2, "the command '%s' failed: %s", command, getWaitStatusDescription(result));
			}
		}
		else try
		{
			// invoking command
			string errorString;
			File pipeFile(command, "pw", errorString);
			if (!errorString.empty())
			{
				_logger->loggedFatal2(subsystem, level,
						format2, "unable to open the pipe '%s': %s", command, errorString);
			}

			pipeFile.put(commandInput);
		}
		catch (...)
		{
			_logger->loggedFatal2(subsystem, level, format2, "%s failed", id);
		}
	}
}

const string WorkerBase::partialDirectorySuffix = "/partial";

}
}

