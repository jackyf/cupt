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
#include <cstdio>
#include <cstring>

#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>

#include <cupt/file.hpp>

#include <internal/common.hpp>

namespace cupt {
namespace internal {

namespace {

int __guarded_fileno(FILE* handle, const string& path)
{
	int fd = fileno(handle);
	if (fd == -1)
	{
		fatal2e(__("%s() failed: '%s'"), "fileno", path);
	}
	return fd;
}

class StorageBuffer
{
 public:
	StorageBuffer(int fd, const string& path, size_t initialSize)
		: p_fd(fd), p_path(path), p_size(initialSize)
	{
		p_storage = new char[p_size];
		p_dataBegin = p_dataEnd = p_storage;
	}
	~StorageBuffer()
	{
		delete [] p_storage;
	}
	bool readMore()
	{
		if (p_dataEnd == p_storage + p_size) grow();

		auto freeSpaceLength = p_storage + p_size - p_dataEnd;

		reading:
		auto readResult = read(p_fd, p_dataEnd, freeSpaceLength);
		if (readResult == -1)
		{
			if (errno != EINTR)
			{
				fatal2e(__("unable to read from the file '%s'"), p_path);
			}
			goto reading;
		}
		p_dataEnd += readResult;

		return readResult;
	}
	void consumeUpTo(char* ptr) { p_dataBegin = ptr; }
	char* getDataBegin() const { return p_dataBegin; }
	char* getDataEnd() const { return p_dataEnd; }
	size_t getDataLength() const { return p_dataEnd - p_dataBegin; }
	void clear() { p_dataBegin = p_dataEnd = p_storage; }
 private:
	int p_fd;
	const string& p_path;
	size_t p_size;
	char* p_storage;
	char* p_dataBegin;
	char* p_dataEnd;

	void grow()
	{
		auto dataLength = getDataLength();
		auto proposedLength = dataLength << 4;

		if (proposedLength > (p_size<<1))
		{
			auto oldStorage = p_storage;
			p_size = proposedLength;
			p_storage = new char[p_size];
			memcpy(p_storage, p_dataBegin, dataLength);
			delete [] oldStorage;
		}
		else
		{
			memmove(p_storage, p_dataBegin, dataLength);
		}
		p_dataBegin = p_storage;
		p_dataEnd = p_dataBegin + dataLength;
	}
};

}

struct FileImpl
{
	FILE* handle;
	const string path;
	bool isPipe;
	bool eof;
	int fd;
	off_t offset;
	unique_ptr< StorageBuffer > readBuffer;

	FileImpl(const string& path_, const char* mode, string& openError);
	~FileImpl();
	size_t unbufferedReadUntil(char, const char**);
	inline size_t getLineImpl(const char**);
	inline void assertFileOpened() const;
};

FileImpl::FileImpl(const string& path_, const char* mode, string& openError)
	: handle(NULL), path(path_), isPipe(false), eof(false), offset(0)
{
	if (mode[0] == 'p')
	{
		if (strlen(mode) != 2)
		{
			fatal2(__("pipe specification mode should be exactly 2 characters"));
		}
		isPipe = true;
		handle = popen(path.c_str(), mode+1);
	}
	else
	{
		// ok, normal file
		handle = fopen(path.c_str(), mode);
	}

	if (!handle)
	{
		openError = format2e("").substr(2);
	}
	else
	{
		// setting FD_CLOEXEC flag

		fd = __guarded_fileno(handle, path);
		int oldFdFlags = fcntl(fd, F_GETFD);
		if (oldFdFlags < 0)
		{
			openError = format2e("unable to get file descriptor flags");
		}
		else
		{
			if (fcntl(fd, F_SETFD, oldFdFlags | FD_CLOEXEC) == -1)
			{
				openError = format2e("unable to set the close-on-exec flag");
			}
		}

		readBuffer.reset(new StorageBuffer(fd, path, 512));
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
				fatal2e(__("unable to close the pipe '%s'"), path);
			}
			else if (pcloseResult)
			{
				fatal2(__("execution of the pipe '%s' failed: %s"), path, getWaitStatusDescription(pcloseResult));
			}
		}
		else
		{
			if (fclose(handle))
			{
				fatal2e(__("unable to close the file '%s'"), path);
			}
		}
	}
}

void FileImpl::assertFileOpened() const
{
	if (!handle)
	{
		// file was not properly opened
		fatal2i("file '%s' was not properly opened", path);
	}
}

size_t FileImpl::unbufferedReadUntil(char delimiter, const char** bufferPtr)
{
	auto& buffer = *readBuffer;

	auto unscannedBegin = buffer.getDataBegin();
	while (true)
	{
		auto unscannedLength = buffer.getDataEnd() - unscannedBegin;
		auto delimiterPtr = static_cast< char* >(memchr(unscannedBegin, delimiter, unscannedLength));
		if (delimiterPtr)
		{
			++delimiterPtr;
			*bufferPtr = buffer.getDataBegin();
			buffer.consumeUpTo(delimiterPtr);
			auto readCount = delimiterPtr - *bufferPtr;
			offset += readCount;
			return readCount;
		}
		else
		{
			auto scannedLength = buffer.getDataLength();
			if (buffer.readMore())
			{
				unscannedBegin = buffer.getDataBegin() + scannedLength;
			}
			else
			{
				auto readCount = buffer.getDataLength();
				eof = (readCount == 0);
				buffer.consumeUpTo(buffer.getDataEnd());
				*bufferPtr = buffer.getDataBegin();
				return readCount;
			}
		}
	}
}

size_t FileImpl::getLineImpl(const char** bufferPtr)
{
	return unbufferedReadUntil('\n', bufferPtr);
}

}

File::File(const string& path, const char* mode, string& openError)
	: __impl(new internal::FileImpl(path, mode, openError))
{}

File::File(File&& other)
	: __impl(other.__impl)
{
	other.__impl = nullptr;
}

File::~File()
{
	delete __impl;
}

File& File::getLine(string& line)
{
	__impl->assertFileOpened();
	const char* buf;
	auto size = __impl->getLineImpl(&buf);
	if (size > 0 && buf[size-1] == '\n')
	{
		--size;
	}
	line.assign(buf, size);
	return *this;
}

File& File::rawGetLine(const char*& buffer, size_t& size)
{
	size = __impl->getLineImpl(&buffer);
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
			fatal2e(__("unable to read from the file '%s'"), __impl->path);
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
	const char* buf;
	while (readLength = __impl->getLineImpl(&buf), readLength > 1)
	{
		record.append(buf, readLength);
	}
	if (!record.empty())
	{
		__impl->eof = false; // last but valid record
	}
	return *this;
}

void File::getFile(string& block)
{
	__impl->assertFileOpened();

	block.clear();

	const char* buf;
	int readLength;
	// readLength of 0 means end of file
	while ((readLength = __impl->getLineImpl(&buf)))
	{
		block.append(buf, readLength);
	}
}

bool File::eof() const
{
	return __impl->eof;
}

void File::seek(size_t newPosition)
{
	if (__impl->isPipe)
	{
		fatal2(__("an attempt to seek on the pipe '%s'"), __impl->path);
	}
	else
	{
		__impl->offset = newPosition;
		if (lseek(__impl->fd, __impl->offset, SEEK_SET) == -1)
		{
			fatal2e(__("unable to seek on the file '%s'"), __impl->path);
		}
		__impl->readBuffer->clear();
	}
}

size_t File::tell() const
{
	if (__impl->isPipe)
	{
		fatal2(__("an attempt to tell a position on the pipe '%s'"), __impl->path);
	}
	return __impl->offset;
}

void File::lock(int flags)
{
	__impl->assertFileOpened();
	// TODO/API break/: provide only lock(void) and unlock(void) methods, consider using fcntl
	if (flock(__impl->fd, flags) == -1)
	{
		auto actionName = (flags & LOCK_UN) ? __("release") : __("obtain");
		fatal2e(__("unable to %s a lock on the file '%s'"), actionName, __impl->path);
	}
}

void File::put(const char* data, size_t size)
{
	__impl->assertFileOpened();
	if (fwrite(data, size, 1, __impl->handle) != 1)
	{
		fatal2e(__("unable to write to the file '%s'"), __impl->path);
	}
}

void File::put(const string& bytes)
{
	put(bytes.c_str(), bytes.size());
}

void File::unbufferedPut(const char* data, size_t size)
{
	fflush(__impl->handle);

	size_t currentOffset = 0;
	while (currentOffset < size)
	{
		auto writeResult = write(__impl->fd, data + currentOffset, size - currentOffset);
		if (writeResult == -1)
		{
			if (errno == EINTR)
			{
				continue;
			}
			else
			{
				fatal2e(__("unable to write to the file '%s'"), __impl->path);
			}
		}
		currentOffset += writeResult;
	}
}

namespace {

File openRequiredFile(const string& path, const char* mode)
{
	string openError;
	File file(path, mode, openError);
	if (!openError.empty())
	{
		fatal2(__("unable to open the file '%s': %s"), path, openError);
	}
	return std::move(file);
}

}

RequiredFile::RequiredFile(const string& path, const char* mode)
	: File(openRequiredFile(path, mode))
{}

} // namespace

