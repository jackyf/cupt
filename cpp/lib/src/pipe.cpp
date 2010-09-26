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
#include <cupt/pipe.hpp>

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
	const char* part = (fd == inputFd ? "input" : "output");
	if (::close(fd) == -1)
	{
		warn("unable to close %s part of '%s' pipe: EEE", part, name.c_str());
	}
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
		fatal("unable to create '%s' pipe: EEE", __data->name.c_str());
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

