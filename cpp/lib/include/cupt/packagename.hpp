/**************************************************************************
*   Copyright (C) 2014 by Eugene V. Lyubimkin                             *
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
#ifndef CUPT_PACKAGENAME_SEEN
#define CUPT_PACKAGENAME_SEEN

#include <cupt/common.hpp>

/*! @file */

namespace cupt {

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
void CUPT_API consumePackageName(const char* begin, const char* end, const char*& resultEnd);

/// checks package name for correctness
/**
 * @param packageName package name
 * @param throwOnError if set to @c true, function will throw exception if @a packageName is not correct
 * @return @c true if the @a packageName is correct, @c false if @a packageName is not correct and @a throwOnError is @c false
 */
bool CUPT_API checkPackageName(const string& packageName, bool throwOnError = true);

}

#endif

