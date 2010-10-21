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
#ifndef CUPT_HASHSUMS_SEEN
#define CUPT_HASHSUMS_SEEN

/// @file

#include <cupt/common.hpp>

namespace cupt {

/// hash sums
class HashSums
{
 public:
	/// hash sum type
	enum Type { MD5, SHA1, SHA256, Count };

	/// array of hash sums
	string values[Count];

	/// shortcut to values[type]
	string& operator[](const Type& type);
	/// shortcut to values[type]
	const string& operator[](const Type& type) const;
	/// does file content match hash sums?
	/**
	 * @param path path to a file
	 * @return @c true if yes, @c false if no
	 */
	bool verify(const string& path) const;
	/// compares with other HashSums object
	/**
	 * @return If there are no hash sums, defined in both objects, returns @c false.
	 * If there are any, returns @c true if all matched and @c false otherwise
	 * @param other object to compare with
	 */
	bool match(const HashSums& other) const;
	/// does object contain no hash sums?
	/**
	 * @return @c true if yes, @c false if no
	 */
	bool empty() const;

	void fill(const string& path);

	static string getHashOfString(const Type& type, const string& pattern);
};

}

#endif

