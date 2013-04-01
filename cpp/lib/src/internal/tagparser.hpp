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
#ifndef CUPT_INTERNAL_TAGPARSER_SEEN
#define CUPT_INTERNAL_TAGPARSER_SEEN

#include <cstring>

#include <cupt/common.hpp>
#include <cupt/fwd.hpp>

#define BUFFER_AND_SIZE(x) x, sizeof(x) - 1

namespace cupt {
namespace internal {

class TagParser
{
 public:
	struct StringRange: public pair< const char*, const char* >
	{
	 public:
		string toString() const
		{
			return string(first, second);
		}
		PackageId toPackageId() const
		{
			return PackageId(first, second-first);
		}
		bool equal(const char* buf, size_t size)
		{
			return ((size_t)(second - first) == size && !std::memcmp(buf, &*first, size));
		}
	};
 private:
	File* const __input;
	const char* __buffer;
	size_t __buffer_size;

	TagParser(const TagParser&);
	TagParser& operator=(const TagParser&);
 public:
	TagParser(File* input);

	bool parseNextLine(StringRange& tagName, StringRange& tagValue);
	// forbidden to call more than once for one tag, since one line
	// (buffer) will be lost between
	void parseAdditionalLines(string& lines);
};

}
}

#endif

