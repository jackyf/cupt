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
#ifndef CUPT_FILE_SEEN
#define CUPT_FILE_SEEN

/// @file

#include <functional>

#include <cupt/common.hpp>

namespace cupt {

namespace internal {

struct FileImpl;

}

/// high-level interface to file routines
class File
{
	internal::FileImpl* __impl;
 public:
	/// constructor
	/**
	 * Constructs new object for a regular file or reading shell pipe.
	 *
	 * @warning You must not use constructed object if @a error is not empty.
	 *
	 * @param path path to file or shell command, see @a mode
	 * @param mode any value, accepted as @a mode in @c fopen(3), or @c "pr" - special value to treat @a path as shell pipe
	 * @param [out] error if open fails, human readable error will be placed here
	 */
	File(const string& path, const char* mode, string& error);
	virtual ~File();
	File& rawGetLine(const char*& buffer, size_t& size);
	File& getLine(string&);
	File& getRecord(string& record, const std::function<bool (const char*, size_t)>& accepter =
			[](const char*, size_t) -> bool { return true; });
	void getFile(string&);
	void put(const string&);
	void put(const char* data, size_t size);

	bool eof() const;
	void seek(size_t newPosition);
	size_t tell() const;

	void lock(int flags);
};

} // namespace

#endif

