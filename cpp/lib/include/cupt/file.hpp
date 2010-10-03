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
	/// destructor
	virtual ~File();
	/// reads new line
	/**
	 * Reads new line (that is, a sequence of characters which ends with newline
	 * character (@c "\n")).
	 *
	 * If the end of file was encountered when reading, newline character will be
	 * not added.
	 *
	 * @param [in, out] buffer will contain a pointer to read data
	 * @param [out] size the size (in bytes) of the buffer, a value @c 0 means end of file
	 * @return reference to self
	 */
	File& rawGetLine(const char*& buffer, size_t& size);
	/// reads new line
	/**
	 * Reads new line. Newline character from the end is strip if present.
	 *
	 * End of file must be checked by querying @ref eof right after @ref getLine. You can use
	 * @a line only if @ref eof returned false.
	 *
	 * @param [out] line container for read data
	 * @return reference to self
	 *
	 * @par Example:
	 * Reading file line by line.
	 * @code
	 * string line;
	 * while (!file.getLine(line).eof())
	 * {
	 *   // process line
	 * }
	 * @endcode
	 */
	File& getLine(string& line);
	/// reads new record
	/**
	 * Reads new record, that is, a sequence of characters which ends with
	 * double newline character (@c "\n\n").
	 *
	 * If the end of file was encountered when reading, newline character(s)
	 * will be not added.
	 *
	 * End of file must be checked by querying @ref eof right after @ref getRecord. You can use
	 * @a record only if @ref eof returned false.
	 *
	 * @param [out] record container for read data
	 * @param accepter an instrument to skip some lines you don't want to be in
	 * a record, to speed up reading. By default accepts all lines. Function
	 * parameters, in the order, are pointer to raw data buffer and its size.
	 * @return reference to self.
	 */
	File& getRecord(string& record, const std::function<bool (const char*, size_t)>& accepter =
			[](const char*, size_t) -> bool { return true; });
	/// reads all available data from current position
	/**
	 * It's usually used just after opening the file and for small files.
	 *
	 * @param block container for read data
	 */
	void getFile(string& block);
	/// writes data
	/**
	 * @param data - data to write
	 */
	void put(const string& data);
	/// writes data
	/**
	 * @param data - pointer to the data buffer
	 * @param size - size of the buffer
	 */
	void put(const char* data, size_t size);

	/// checks for the end of file condition
	bool eof() const;
	/// seeks to a new position
	/**
	 * Sets new position of the file to write/read.
	 * Will throw Exception if called against File opened as pipe.
	 *
	 * @param newPosition new file position
	 */
	void seek(size_t newPosition);
	/// gets current file position
	size_t tell() const;

	void lock(int flags);
};

} // namespace

#endif

