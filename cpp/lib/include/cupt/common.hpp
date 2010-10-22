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

/*! @file */

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

/** @namespace cupt */
namespace cupt {

extern const char* const libraryVersion; ///< the version of Cupt library

using std::vector;
using std::string;

/// general library exception class
/**
 * Any library function may throw this exception.
 */
class Exception: public std::runtime_error
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

/// message file descriptor
/**
 * All library error, warning, debug and simulate messages will be pointed here.
 * If @a messageFd @c == @c -1, messages will be suppressed. Defaults to @c -1.
 */
extern int messageFd;

/// sends an error message and throws exception
/**
 * This function:
 *  -# substitutes at most one @c "EEE" substring (leftest one) in @a format
 *  -# perform @c printf against computed string with variable arguments
 *  -# writes string @c "E:" + computed string + @c "\n" to @ref messageFd
 *  -# throws Exception with computed string as message
 *  .
 * @param format @c printf format string (see @c printf(3))
 *
 * @par Example:
 * @code
 * if (!fopen("abcd.dat", "r"))
 * {
 *   fatal("unable to open file '%s': EEE", "abcd.dat");
 * }
 * @endcode
 * may send @c "E: unable to open file 'abcd.dat': Permission denied\n" to
 * @ref messageFd and throw Exception with message @c "unable to open
 * file 'abcd.dat': Permission denied"
 */
void fatal(const char* format, ...);

/// sends a warning message
/**
 * This function:
 *  -# substitutes at most one @c "EEE" substring (leftest one) in @a format
 *  -# perform @c printf against computed string with variable arguments
 *  -# writes string @c "W:" + computed string + @c "\n" to @ref messageFd
 *  .
 * @param format @c printf format string
 *
 * @see fatal
 */
void warn(const char* format, ...);

/// sends a debug message
/**
 * Equal to @ref warn, only sends @c "D:" instead of @c "W:"
 */
void debug(const char* format, ...);

/// sends a simulate message
/**
 * Equal to @ref warn, only sends @c "S:" instead of @c "W:"
 */
void simulate(const char* format, ...);

/// @cond
string sf(const string& format, ...);
vector< string > split(char, const string&, bool allowEmpty = false);
string join(const string& joiner, const vector< string >& parts);
string humanReadableSizeString(uint64_t bytes);
string globToRegexString(const string&);
/// @endcond

/// localizes message
/**
 * @param message input string
 * @return localized message
 */
string __(const char* message);

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
void consumePackageName(string::const_iterator begin, string::const_iterator end,
		string::const_iterator& resultEnd);

/// checks package name for correctness
/**
 * @param packageName package name
 * @param throwOnError if set to @c true, function will throw exception if @a packageName is not correct
 * @return @c true if the @a packageName is correct, @c false if @a packageName is not correct and @a throwOnError is @c false
 */
bool checkPackageName(const string& packageName, bool throwOnError = true);

/// checks version string for correctness
/**
 * Equal to @ref checkPackageName, only checks version string instead of package name
 */
bool checkVersionString(const string& versionString, bool throwOnError = true);

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
int compareVersionStrings(const string& left, const string& right);

} // namespace

#endif

