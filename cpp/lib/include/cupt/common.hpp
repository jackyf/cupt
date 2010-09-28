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
	Exception(const char* message)
		: std::runtime_error(message)
	{}
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
 * Points to standard error stream by default.
 */
extern int messageFd;

/// sends an error message and throws exception
/**
 * This function:
 *  -# substitutes all @c "EEE" substrings in format
 *  -# perform printf against computed string with variable arguments
 *  -# writes string @c "E:" + computed string + @c "\n" to messageFd
 *  -# throws Exception with computed string as message
 *  .
 * @param [in] format printf format string (see printf(3))
 *
 * Example:
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
 *  -# substitutes all @c "EEE" substrings in format
 *  -# perform printf against computed string with variable arguments
 *  -# writes string @c "W:" + computed string + @c "\n" to messageFd
 *  .
 * @param [in] format printf format string (see printf(3))
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
string sf(const string& format, ...);
vector< string > split(char, const string&, bool allowEmpty = false);
string join(const string& joiner, const vector< string >& parts);
string humanReadableSizeString(uint64_t bytes);
string globToRegexString(const string&);

string __(const char*);

void consumePackageName(string::const_iterator begin, string::const_iterator end,
		string::const_iterator& resultEnd);
bool checkPackageName(const string& packageName, bool throwOnError = true);
bool checkVersionString(const string& versionString, bool throwOnError = true);

int compareVersionStrings(const string&, const string&);

} // namespace

#endif

