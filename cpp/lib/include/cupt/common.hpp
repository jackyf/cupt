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

#ifndef FORIT
#define FORIT(variableName, storage) for (auto variableName = (storage).begin(); variableName != (storage).end(); ++variableName)
#endif

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
void fatal(const char* format, ...);
void warn(const char* format, ...);
void debug(const char* format, ...);
void simulate(const char* format, ...);
string sf(const string& format, ...);
vector< string > split(char, const string&, bool allowEmpty = false);
string join(const string& joiner, const vector< string >& parts);
string humanReadableSizeString(uint64_t bytes);
string globToRegexString(const string&);

string __(const char*);

template < class T >
struct PointerLess
{
	bool operator()(const shared_ptr< T >& left, const shared_ptr< T >& right) const
	{
		return *left < *right;
	}
	bool operator()(const T* left, const T* right) const
	{
		return *left < *right;
	}
};
template < class T >
struct PointerEqual: public std::binary_function< shared_ptr< T >, shared_ptr< T >, bool >
{
	bool operator()(const shared_ptr< T >& left, const shared_ptr< T >& right) const
	{
		return *left == *right;
	}
	bool operator()(const T* left, const T* right) const
	{
		return *left == *right;
	}
};

void consumePackageName(string::const_iterator begin, string::const_iterator end,
		string::const_iterator& resultEnd);
bool checkPackageName(const string& packageName, bool throwOnError = true);
bool checkVersionString(const string& versionString, bool throwOnError = true);

int compareVersionStrings(const string&, const string&);

} // namespace

#endif

