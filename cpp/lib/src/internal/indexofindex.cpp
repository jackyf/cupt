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
#include <internal/tagparser.hpp>

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

void parsePackagesSourcesFullIndex(const string& path, const ps::Callbacks& callbacks, const Record& record)
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
			record.indexStringPtr->assign(buf + packageAnchorLength, size - packageAnchorLength - 1);
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

void parseTranslationFullIndex(const string& path, const tr::Callbacks& callbacks, const Record& record)
{
	RequiredFile file(path, "r");

	TagParser parser(&file);
	TagParser::StringRange tagName, tagValue;

	static const char descriptionSubPattern[] = "Description-";
	static const size_t descriptionSubPatternSize = sizeof(descriptionSubPattern) - 1;

	size_t recordPosition;

	while (true)
	{
		recordPosition = file.tell();
		if (!parser.parseNextLine(tagName, tagValue))
		{
			if (file.eof()) break; else continue;
		}

		bool hashSumFound = false;
		bool translationFound = false;

		do
		{
			if (tagName.equal(BUFFER_AND_SIZE("Description-md5")))
			{
				hashSumFound = true;
				*record.indexStringPtr = tagValue.toString();
			}
			else if ((size_t)(tagName.second - tagName.first) > descriptionSubPatternSize &&
					!memcmp(&*tagName.first, descriptionSubPattern, descriptionSubPatternSize))
			{
				translationFound = true;
				*record.offsetPtr = file.tell() - (tagValue.second - tagValue.first) - 1; // -1 for '\n'
			}
		} while (parser.parseNextLine(tagName, tagValue));

		if (!hashSumFound)
		{
			fatal2(__("unable to find the md5 hash in the record starting at byte '%u'"), recordPosition);
		}
		if (!translationFound)
		{
			fatal2(__("unable to find the translation in the record starting at byte '%u'"), recordPosition);
		}

		callbacks.main();
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

template< typename Callbacks, typename AdditionalLinesParser >
void templatedParseIndexOfIndex(const string& path, const Callbacks& callbacks, const Record& record,
		const AdditionalLinesParser& additionalLinesParser)
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
				fatal2i("ioi: offset and index string: too small line");
			}
			// finding delimiter (format: "<hex>\0<packagename>\n)
			auto delimiterPosition = memchr(buf+1, '\0', bufSize-3);
			if (!delimiterPosition)
			{
				fatal2i("ioi: offset and index string: no delimiter found");
			}

			absoluteOffset += ourHex2Uint(buf);
			(*record.offsetPtr) = absoluteOffset;
			record.indexStringPtr->assign((const char*)delimiterPosition+1, buf+bufSize-1);
			callbacks.main();
		}
		while (file.rawGetLine(buf, bufSize), bufSize > 1)
		{
			auto fieldType = buf[0];
			additionalLinesParser(fieldType, buf+1, buf+bufSize-1);
		}
	}
}

void parsePackagesSourcesIndexOfIndex(const string& path, const ps::Callbacks& callbacks, const Record& record)
{
	auto additionalLinesParser = [&callbacks](char fieldType, const char* bufferStart, const char* bufferEnd)
	{
		switch (fieldType)
		{
			case field::provides:
				callbacks.provides(bufferStart, bufferEnd);
				break;
			default:
				fatal2i("ioi: invalid field type %zu", size_t(fieldType));
		}
	};
	templatedParseIndexOfIndex(path, callbacks, record, additionalLinesParser);
}

void parseTranslationIndexOfIndex(const string& path, const tr::Callbacks& callbacks, const Record& record)
{
	auto additionalLinesParser = [](char fieldType, const char*, const char*)
	{
		fatal2i("ioi: unexpected additional field type %zu", size_t(fieldType));
	};
	templatedParseIndexOfIndex(path, callbacks, record, additionalLinesParser);
}

static const string indexPathSuffix = ".index" "0";

void putUint2Hex(File& file, uint32_t value)
{
	char buf[sizeof(value)*2 + 1];

	file.put(buf, sprintf(buf, "%x", value));
}

struct MainCallback
{
	string indexString;
	uint32_t previousOffset;
	uint32_t offset;
	bool isFirstRecord;

	MainCallback()
		: previousOffset(0)
		, isFirstRecord(true)
	{}

	void perform(File& file)
	{
		if (!isFirstRecord) file.put("\n");
		isFirstRecord = false;

		auto relativeOffset = offset - previousOffset;
		putUint2Hex(file, relativeOffset);
		file.put("\0", 1);
		file.put(indexString);
		file.put("\n");

		previousOffset = offset;
	}
};

template < typename CallbacksPreFiller, typename FullIndexParser >
void templatedGenerate(const string& indexPath, const string& temporaryPath,
		const CallbacksPreFiller& callbacksPreFiller, FullIndexParser fullIndexParser)
{
	RequiredFile file(temporaryPath, "w");

	auto callbacks = callbacksPreFiller(file);
	MainCallback mainCallback;
	callbacks.main = std::bind(&MainCallback::perform, std::ref(mainCallback), std::ref(file));

	fullIndexParser(indexPath, callbacks,
			{ &mainCallback.offset, &mainCallback.indexString });

	fs::move(temporaryPath, getIndexOfIndexPath(indexPath));
}

template< typename Callbacks, typename Parser >
void templatedProcessIndex(const string& path, const Callbacks& callbacks, const Record& record,
		Parser fullParser, Parser ioiParser)
{
	auto ioiPath = getIndexOfIndexPath(path);
	if (fs::fileExists(ioiPath) && (getModifyTime(ioiPath) >= getModifyTime(path)))
	{
		ioiParser(ioiPath, callbacks, record);
	}
	else
	{
		fullParser(path, callbacks, record);
	}
}

}

string getIndexOfIndexPath(const string& path)
{
	return path + indexPathSuffix;
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

namespace ps {

void processIndex(const string& path, const Callbacks& callbacks, const Record& record)
{
	templatedProcessIndex(path, callbacks, record,
			parsePackagesSourcesFullIndex, parsePackagesSourcesIndexOfIndex);
}

void generate(const string& indexPath, const string& temporaryPath)
{
	auto callbacksPreFiller = [](File& file)
	{
		Callbacks callbacks;
		callbacks.provides =
				[&file](const char* begin, const char* end)
				{
					file.put(&field::provides, 1);
					file.put(begin, end - begin);
					file.put("\n");
				};
		return callbacks;
	};
	templatedGenerate(indexPath, temporaryPath, callbacksPreFiller, parsePackagesSourcesFullIndex);
}

}

namespace tr {

void processIndex(const string& path, const Callbacks& callbacks, const Record& record)
{
	templatedProcessIndex(path, callbacks, record,
			parseTranslationFullIndex, parseTranslationIndexOfIndex);
}

void generate(const string& indexPath, const string& temporaryPath)
{
	auto callbacksPreFiller = [](File&) { return Callbacks(); };
	templatedGenerate(indexPath, temporaryPath, callbacksPreFiller, parseTranslationFullIndex);
}

}

}
}
}

