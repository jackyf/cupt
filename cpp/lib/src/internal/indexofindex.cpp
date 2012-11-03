/**************************************************************************
*   Copyright (C) 2012 by Eugene V. Lyubimkin                             *
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
// for stat
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cupt/file.hpp>

#include <internal/filesystem.hpp>

#include <internal/indexofindex.hpp>

namespace cupt {
namespace internal {
namespace ioi {

namespace {

time_t getModifyTime(const string& path)
{
	struct stat st;
	auto error = stat(path.c_str(), &st);
	if (error) return 0;
	return st.st_mtime;
}

void parseFullIndex(const string& path, const Callbacks& callbacks, const Record& record)
{
	RequiredFile file(path, "r");

	uint32_t offset = 0;

	while (true)
	{
		const char* buf;
		size_t size;
		auto getNextLine = [&file, &buf, &size, &offset]
		{
			file.rawGetLine(buf, size);
			offset += size;
		};

		getNextLine();
		if (size == 0)
		{
			break; // eof
		}
		*(record.offsetPtr) = offset;

		static const size_t packageAnchorLength = sizeof("Package: ") - 1;
		if (size > packageAnchorLength && !memcmp("Package: ", buf, packageAnchorLength))
		{
			record.packageNamePtr->assign(buf + packageAnchorLength, size - packageAnchorLength - 1);
		}
		else
		{
			fatal2(__("unable to find a Package line"));
		}
		callbacks.main();

		while (getNextLine(), size > 1)
		{
			static const size_t providesAnchorLength = sizeof("Provides: ") - 1;
			if (*buf == 'P' && size > providesAnchorLength && !memcmp("rovides: ", buf+1, providesAnchorLength-1))
			{
				callbacks.provides(buf + providesAnchorLength, buf + size - 1);
			}
		}
	}
}

namespace field {

const char provides = 'p';

}

uint32_t ourHex2Uint(const char* s)
{
	uint32_t result = 0;
	do
	{
		uint8_t hexdigit;
		if (*s >= '0' && *s <= '9')
		{
			hexdigit = *s - '0';
		}
		else if (*s >= 'a' && *s <= 'f')
		{
			hexdigit = *s - 'a' + 10;
		}
		else
		{
			fatal2i("ioi: offset: non-hex character '%c'", *s);
		}

		result = (result << 4) + hexdigit;
	} while (*(++s));

	return result;
}

void parseIndexOfIndex(const string& path, const Callbacks& callbacks, const Record& record)
{
	RequiredFile file(path, "r");

	uint32_t absoluteOffset = 0;

	const char* buf;
	size_t bufSize;
	while (file.rawGetLine(buf, bufSize), bufSize > 0)
	{
		{ // offset and package name:
			if (bufSize-1 < 3)
			{
				fatal2i("ioi: offset and package name: too small line");
			}
			// finding delimiter (format: "<hex>\0<packagename>\n)
			auto delimiterPosition = memchr(buf+1, '\0', bufSize-3);
			if (!delimiterPosition)
			{
				fatal2i("ioi: offset and package name: no delimiter found");
			}

			absoluteOffset += ourHex2Uint(buf);
			(*record.offsetPtr) = absoluteOffset;
			record.packageNamePtr->assign((const char*)delimiterPosition+1, buf+bufSize-1);
			callbacks.main();
		}
		while (file.rawGetLine(buf, bufSize), bufSize > 1)
		{
			auto fieldType = buf[0];
			switch (fieldType)
			{
				case field::provides:
					callbacks.provides(buf+1, buf+bufSize-1);
					break;
				default:
					fatal2i("ioi: invalid field type %zu", size_t(fieldType));
			}
		}
	}
}

static const string indexPathSuffix = ".index" "0";

void putUint2Hex(File& file, uint32_t value)
{
	char buf[sizeof(value)*2 + 1];

	file.put(buf, sprintf(buf, "%x", value));
}

}

string getIndexOfIndexPath(const string& path)
{
	return path + indexPathSuffix;
}

void processIndex(const string& path, const Callbacks& callbacks, const Record& record)
{
	auto ioiPath = getIndexOfIndexPath(path);
	if (fs::fileExists(ioiPath) && (getModifyTime(ioiPath) >= getModifyTime(path)))
	{
		parseIndexOfIndex(ioiPath, callbacks, record);
	}
	else
	{
		parseFullIndex(path, callbacks, record);
	}
}

void removeIndexOfIndex(const string& path)
{
	auto ioiPath = getIndexOfIndexPath(path);
	if (fs::fileExists(ioiPath))
	{
		if (unlink(ioiPath.c_str()) == -1)
		{
			fatal2e("unable to remove the file '%s'", ioiPath);
		}
	}
}

void generate(const string& indexPath, const string& temporaryPath)
{
	RequiredFile file(temporaryPath, "w");

	string packageName;
	uint32_t previousOffset = 0;
	uint32_t offset;
	bool isFirstRecord = true;

	Callbacks callbacks;
	callbacks.main =
			[&file, &isFirstRecord, &offset, &previousOffset, &packageName]()
			{
				if (!isFirstRecord) file.put("\n");
				isFirstRecord = false;

				auto relativeOffset = offset - previousOffset;
				putUint2Hex(file, relativeOffset);
				file.put("\0", 1);
				file.put(packageName);
				file.put("\n");

				previousOffset = offset;
			};
	callbacks.provides =
			[&file](const char* begin, const char* end)
			{
				file.put(&field::provides, 1);
				file.put(begin, end - begin);
				file.put("\n");
			};

	parseFullIndex(indexPath, callbacks, { &offset, &packageName });

	fs::move(temporaryPath, getIndexOfIndexPath(indexPath));
}

}
}
}

