/**************************************************************************
*   Copyright (C) 2010-2012 by Eugene V. Lyubimkin                        *
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
#include <fcntl.h>

#include <internal/pipe.hpp>

namespace cupt {
namespace internal {

struct PipeData
{
	int inputFd;
	int outputFd;
	int* usedFdPtr;
	string name;

	void close(int);
};

void PipeData::close(int fd)
{
	if (::close(fd) == -1)
	{
		warn2e(__("unable to close a part of the '%s' pipe"), name);
	}
}

Pipe::Pipe(const string& name_)
	: __data(new internal::PipeData)
{
	__data->usedFdPtr = NULL;
	__data->name = name_;
	int pipeFdPair[2];
	if (pipe(pipeFdPair) == -1)
	{
		fatal2e(__("unable to create the '%s' pipe"), __data->name);
	}

	// setting FD_CLOEXEC flags
	for (size_t i = 0; i < 2; ++i)
	{
		int fd = pipeFdPair[i];
		int oldFdFlags = fcntl(fd, F_GETFD);
		if (oldFdFlags < 0)
		{
			fatal2e(__("unable to create the '%s' pipe: unable to get file descriptor flags"), __data->name);
		}
		if (fcntl(fd, F_SETFD, oldFdFlags | FD_CLOEXEC) == -1)
		{
			fatal2e(__("unable to create the '%s' pipe: unable to set the close-on-exec flag"), __data->name);
		}
	}

	__data->inputFd = pipeFdPair[0];
	__data->outputFd = pipeFdPair[1];
}

void Pipe::useAsReader()
{
	__data->usedFdPtr = &__data->inputFd;
	__data->close(__data->outputFd);
}

void Pipe::useAsWriter()
{
	__data->usedFdPtr = &__data->outputFd;
	__data->close(__data->inputFd);
}

int Pipe::getReaderFd()
{
	return __data->inputFd;
}

int Pipe::getWriterFd()
{
	return __data->outputFd;
}

Pipe::~Pipe()
{
	if (__data->usedFdPtr)
	{
		__data->close(*__data->usedFdPtr);
	}
	else
	{
		// both used
		__data->close(__data->inputFd);
		__data->close(__data->outputFd);
	}

	delete __data;
}

}
}

