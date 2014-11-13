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
#include <unistd.h>

#include <thread>

#include <cupt/config.hpp>
#include <cupt/versionstring.hpp>

#include <internal/lock.hpp>
#include <internal/common.hpp>
#include <internal/pipe.hpp>

#include <internal/worker/base.hpp>

namespace cupt {
namespace internal {

Worker::Action::Type WorkerBase::_download_dependent_action_types[] = {
		Action::Reinstall, Action::Install, Action::Upgrade, Action::Downgrade
};

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
	return version->packageName + '_' +
			versionstring::getOriginal(version->versionString).toStdString()
			+ '_' + version->architecture + ".deb";
}

void WorkerBase::_run_external_command(Logger::Subsystem subsystem,
		const string& command, const CommandInput& commandInput, const string& errorId)
{
	const Logger::Level level = 3;
	_logger->log(subsystem, level, format2("running: %s", command));

	if (_config->getBool("cupt::worker::simulate"))
	{
		if (commandInput.buffer.empty())
		{
			simulate2("running command '%s'", command);
		}
		else
		{
			simulate2("running command '%s' with the input\n-8<-\n%s\n->8-",
					command, commandInput.buffer);
		}
	}
	else
	{
		const string& id = (errorId.empty() ? command : errorId);

		if (commandInput.buffer.empty())
		{
			p_invokeShellCommand(subsystem, level, command);
		}
		else try
		{
			p_runCommandWithInput(subsystem, level, command, commandInput);
		}
		catch (...)
		{
			_logger->loggedFatal2(subsystem, level, format2, "%s failed", id);
		}
	}
}

void WorkerBase::p_invokeShellCommand(Logger::Subsystem subsystem, Logger::Level level, const string& command)
{
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

static void writeBufferToFd(Pipe& pipe, const string& buffer, const string& pipeName)
{
	int fd = pipe.getWriterFd();
	const char* bufferCursor = buffer.data();
	const char* bufferEnd = bufferCursor + buffer.size();

	while (bufferCursor < bufferEnd)
	{
		auto writeResult = ::write(fd, bufferCursor, bufferEnd-bufferCursor);
		if (writeResult == -1)
		{
			warn2e(__("unable to write to the file '%s'"), pipeName);
			return;
		}

		bufferCursor += writeResult;
	}
	pipe.useAsReader(); // close the writer fd
}

class ScopedThread
{
 public:
	ScopedThread(std::thread&& t)
		: _t(std::move(t))
	{}
	~ScopedThread()
	{
		_t.join();
	}
 private:
	std::thread _t;
};

static inline string getRedirectionSuffix(int fromFd, int toFd)
{
	return format2("%zu<&%zu", toFd, fromFd);
}

void WorkerBase::p_runCommandWithInput(Logger::Subsystem subsystem, Logger::Level level,
		const string& command, const CommandInput& input)
{
	auto inputPipeName = format2("input stream for '%s'", command);
	unique_ptr<Pipe> inputPipe(new Pipe(inputPipeName, true));

	ScopedThread inputStreamThread(std::thread(writeBufferToFd,
			std::ref(*inputPipe), std::cref(input.buffer), std::cref(inputPipeName)));

	auto fdAmendedCommand = format2("bash -c '(%s) %s'", command, getRedirectionSuffix(inputPipe->getReaderFd(), input.fd));
	p_invokeShellCommand(subsystem, level, fdAmendedCommand);
}

const string WorkerBase::partialDirectorySuffix = "/partial";

}
}

