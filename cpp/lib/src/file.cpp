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
#include <cstdio>
#include <cstring>

#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>

#include <cupt/file.hpp>

#include <internal/common.hpp>

namespace cupt {

static int __guarded_fileno(FILE* handle, const string& path)
{
	int fd = fileno(handle);
	if (fd == -1)
	{
		fatal("fileno on file '%s' failed: EEE", path.c_str());
	}
	return fd;
}

namespace internal {

struct FileImpl
{
	FILE* handle;
	char* buf; // for ::getline
	size_t bufLength; // for ::getline
	const string path;
	bool isPipe;

	FileImpl(const string& path_, const char* mode, string& openError);
	~FileImpl();
	inline size_t getLineImpl();
	inline void assertFileOpened() const;
};

FileImpl::FileImpl(const string& path_, const char* mode, string& openError)
	: handle(NULL), buf(NULL), bufLength(0), path(path_), isPipe(false)
{
	if (std::strcmp(mode, "pr") == 0)
	{
		// need to open read pipe
		isPipe = true;
		handle = popen(path.c_str(), "r");
	}
	else
	{
		// ok, normal file
		handle = fopen(path.c_str(), mode);
	}

	if (!handle)
	{
		openError = sf("EEE");
	}
	else
	{
		// setting FD_CLOEXEC flag

		int fd = __guarded_fileno(handle, path);
		int oldFdFlags = fcntl(fd, F_GETFD);
		if (oldFdFlags < 0)
		{
			openError = sf("unable to get file descriptor flags: EEE");
		}
		else
		{
			if (fcntl(fd, F_SETFD, oldFdFlags | FD_CLOEXEC) == -1)
			{
				openError = sf("unable to set the close-on-exec flag: EEE");
			}
		}
	}
}

FileImpl::~FileImpl()
{
	if (handle)
	{
		if (isPipe)
		{
			auto pcloseResult = pclose(handle);
			if (pcloseResult == -1)
			{
				fatal("unable to close pipe '%s': EEE", path.c_str());
			}
			else if (pcloseResult)
			{
				fatal("pipe '%s' execution failed: %s", path.c_str(), getWaitStatusDescription(pcloseResult).c_str());
			}
		}
		else
		{
			if (fclose(handle))
			{
				fatal("unable to close file '%s': EEE", path.c_str());
			}
		}
	}
	free(buf);
}

void FileImpl::assertFileOpened() const
{
	if (!handle)
	{
		// file was not properly opened
		fatal("internal error: file '%s' was not properly opened", path.c_str());
	}
}

size_t FileImpl::getLineImpl()
{
	auto result = ::getline(&buf, &bufLength, handle);
	if (result >= 0)
	{
		return result;
	}
	else
	{
		// an error occured
		if (!feof(handle))
		{
			// real error
			fatal("unable to read from file '%s': EEE", path.c_str());
		}

		// ok, end of file
		return 0;
	}
}

}

File::File(const string& path, const char* mode, string& openError)
	: __impl(new internal::FileImpl(path, mode, openError))
{}

File::~File()
{
	delete __impl;
}

File& File::getLine(string& line)
{
	__impl->assertFileOpened();
	auto size = __impl->getLineImpl();
	if (size > 0 && __impl->buf[size-1] == '\n')
	{
		--size;
	}
	line.assign(__impl->buf, size);
	return *this;
}

File& File::rawGetLine(const char*& buffer, size_t& size)
{
	size = __impl->getLineImpl();
	buffer = __impl->buf;
	return *this;
}

File& File::getBlock(char* buffer, size_t& size)
{
	__impl->assertFileOpened();

	size = ::fread(buffer, 1, size, __impl->handle);
	if (!size)
	{
		// an error occured
		if (!feof(__impl->handle))
		{
			// real error
			fatal("unable to read from file '%s': EEE", __impl->path.c_str());
		}
	}
	return *this;
}

File& File::getRecord(string& record)
{
	__impl->assertFileOpened();

	record.clear();

	int readLength;
	// readLength of 0 means end of file, of 1 - end of record
	while (readLength = __impl->getLineImpl(), readLength > 1)
	{
		record.append(__impl->buf, readLength);
	}
	if (!record.empty())
	{
		// there was some info read, clear eof flag before the next getRecord/getLine call
		clearerr(__impl->handle);
	}
	return *this;
}

void File::getFile(string& block)
{
	__impl->assertFileOpened();

	block.clear();

	int readLength;
	// readLength of 0 means end of file
	while ((readLength = __impl->getLineImpl()))
	{
		block.append(__impl->buf, readLength);
	}
}

bool File::eof() const
{
	__impl->assertFileOpened();
	return feof(__impl->handle);
}

void File::seek(size_t newPosition)
{
	if (__impl->isPipe)
	{
		fatal("an attempt to seek on pipe '%s'", __impl->path.c_str());
	}
	else
	{
		if (fseek(__impl->handle, newPosition, SEEK_SET) == -1)
		{
			fatal("unable to seek on file '%s': EEE", __impl->path.c_str());
		}
	}
}

size_t File::tell() const
{
	if (__impl->isPipe)
	{
		fatal("an attempt to tell position on pipe '%s'", __impl->path.c_str());
	}
	else
	{
		long result = ftell(__impl->handle);
		if (result == -1)
		{
			fatal("unable to tell position on file '%s': EEE", __impl->path.c_str());
		}
		else
		{
			return result;
		}
	}
	__builtin_unreachable();
}

void File::lock(int flags)
{
	__impl->assertFileOpened();
	int fd = __guarded_fileno(__impl->handle, __impl->path);
	// TODO/2.1: consider using fcntl
	if (flock(fd, flags) == -1)
	{
		const char* actionName = (flags & LOCK_UN) ? "release" : "obtain";
		fatal("unable to %s lock on file '%s': EEE", actionName, __impl->path.c_str());
	}
}

void File::put(const char* data, size_t size)
{
	__impl->assertFileOpened();
	if (fwrite(data, size, 1, __impl->handle) != 1)
	{
		fatal("unable to write to file '%s': EEE", __impl->path.c_str());
	}
}

void File::put(const string& bytes)
{
	put(bytes.c_str(), bytes.size());
}

} // namespace

