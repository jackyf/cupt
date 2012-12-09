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
#ifndef CUPT_COMMON_SEEN
#define CUPT_COMMON_SEEN

/// @cond
#define CUPT_API __attribute__ ((visibility("default")))
#define CUPT_LOCAL __attribute__ ((visibility("hidden")))
/// @endcond

/*! @file */

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

/** @namespace cupt */
namespace cupt {

CUPT_API extern const char* const libraryVersion; ///< the version of Cupt library

using std::vector;
using std::string;

/// general library exception class
/**
 * Any library function may throw this exception.
 */
class CUPT_API Exception: public std::runtime_error
{
 public:
	/// constructor
	/**
	 * Creates Exception object with a message @a message.
	 *
	 * @param message human-readable exception description
	 */
	Exception(const char* message)
		: std::runtime_error(message)
	{}
	/// constructor
	/**
	 * @copydoc Exception(const char*)
	 */
	Exception(const string& message)
		: std::runtime_error(message)
	{}
};

using std::pair;

using std::shared_ptr;
using std::static_pointer_cast;
using std::dynamic_pointer_cast;
using std::unique_ptr;

/// message file descriptor
/**
 * All library error, warning, debug and simulate messages will be pointed here.
 * If @a messageFd @c == @c -1, messages will be suppressed. Defaults to @c -1.
 */
CUPT_API extern int messageFd;

/// @cond
CUPT_API string join(const string& joiner, const vector< string >& parts);
CUPT_API string humanReadableSizeString(uint64_t bytes);
/// @endcond

/// localizes message
/**
 * @param message input string
 * @return localized message
 */
CUPT_API const char* __(const char* message);

/// reads package name in range
/**
 * Tries to read as more characters as possible from the @a begin, which form a
 * valid package name, until @a end.
 *
 * @param begin range begin iterator
 * @param end range end iterator
 * @param [in,out] resultEnd consumed range end iterator
 *
 * @par Example:
 * @code
 * string input = "zzuf (>= 1.2)";
 * string::const_iterator resultEnd;
 * consumePackageName(input.begin(), input.end(), resultEnd);
 * cout << string(input.begin(), resultEnd) << endl;
 * @endcode
 * @c "zzuf" will be printed
 */
void CUPT_API consumePackageName(string::const_iterator begin, string::const_iterator end,
		string::const_iterator& resultEnd);

/// checks package name for correctness
/**
 * @param packageName package name
 * @param throwOnError if set to @c true, function will throw exception if @a packageName is not correct
 * @return @c true if the @a packageName is correct, @c false if @a packageName is not correct and @a throwOnError is @c false
 */
bool CUPT_API checkPackageName(const string& packageName, bool throwOnError = true);

/// checks version string for correctness
/**
 * Equal to @ref checkPackageName, only checks version string instead of package name
 */
bool CUPT_API checkVersionString(const string& versionString, bool throwOnError = true);

/// compares two version strings
/**
 * @param left left version string
 * @param right right version string
 * @return @c -1, if @a left @c < @a right, @c 0 if @a left @c == @a right, @c 1 if @a left @c > @a right
 * @note
 * The version strings may be logically equal even if they are not physically
 * equal. Unless you are comparing version strings that belong to the same
 * cache::Package, you should use this function to test their equality.
 */
int CUPT_API compareVersionStrings(const string& left, const string& right);

} // namespace

#include <cupt/format2.hpp>

#endif

