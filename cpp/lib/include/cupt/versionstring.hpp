/**************************************************************************
*   Copyright (C) 2010-2014 by Eugene V. Lyubimkin                        *
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
#ifndef CUPT_VERSIONSTRING_SEEN
#define CUPT_VERSIONSTRING_SEEN

#include <cupt/common.hpp>
#include <cupt/stringrange.hpp>

namespace cupt {

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

/// gets the original part of possibly Cupt-modified version string
/**
 * Cupt may apply Cupt-specific id suffixes to original version strings for
 * its own use, mainly for distinguishing version which declare same version
 * strings in the metadata but (possibly) having different content.
 *
 * This function tells the version string as it was seen in the metadata.
 *
 * @param versionString
 */
CUPT_API StringRange getOriginalVersionString(const StringRange& versionString);

}

#endif

