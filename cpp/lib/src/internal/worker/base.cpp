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
#include <cupt/config.hpp>
#include <cupt/file.hpp>

#include <internal/lock.hpp>
#include <internal/common.hpp>

#include <internal/worker/base.hpp>

namespace cupt {
namespace internal {

WorkerBase::WorkerBase()
{
	fatal("internal error: WorkerBase::WorkerBase shouldn't be ever called");
}

WorkerBase::WorkerBase(const shared_ptr< const Config >& config, const shared_ptr< const Cache >& cache)
	: _config(config), _cache(cache)
{
	_logger = new Logger(*_config);

	__umask = umask(0022);

	string lockPath = _config->getPath("cupt::directory::state") + "/lock";
	__lock = new Lock(_config, lockPath);
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

string WorkerBase::_get_archive_basename(const shared_ptr< const BinaryVersion >& version)
{
	return version->packageName + '_' + version->versionString + '_' +
			version->architecture + ".deb";
}

void WorkerBase::_run_external_command(Logger::Subsystem subsystem,
		const string& command, const string& commandInput, const string& errorId)
{
	_logger->log(subsystem, 3, sf("running: %s", command.c_str()));

	if (_config->getBool("cupt::worker::simulate"))
	{
		if (commandInput.empty())
		{
			simulate("running command '%s'", command.c_str());
		}
		else
		{
			simulate("running command '%s' with the input\n-8<-\n%s\n->8-",
					command.c_str(), commandInput.c_str());
		}
	}
	else
	{
		const char* id = (errorId.empty() ? command.c_str() : errorId.c_str());

		if (commandInput.empty())
		{
			// invoking command
			auto result = ::system(command.c_str());
			if (result == -1)
			{
				fatal("unable to launch command '%s': EEE",
						command.c_str());
			}
			else if (result)
			{
				fatal("command '%s' execution failed: %s", command.c_str(),
						getWaitStatusDescription(result).c_str());
			}
		}
		else try
		{
			// invoking command
			string errorString;
			File pipeFile(command, "pw", errorString);
			if (!errorString.empty())
			{
				fatal("unable to launch a pipe to the command '%s': %s",
						command.c_str(), errorString.c_str());
			}

			pipeFile.put(commandInput);
		}
		catch (...)
		{
			fatal("%s failed", id);
		}
	}
}

const string WorkerBase::partialDirectorySuffix = "/partial";

}
}

