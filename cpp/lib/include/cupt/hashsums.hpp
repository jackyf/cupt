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

#include <cupt/common.hpp>

namespace cupt {

class HashSums
{
 public:
	enum Type { MD5, SHA1, SHA256, Count };

	string values[Count];

	string& operator[](const Type& type);
	const string& operator[](const Type& type) const;
	bool verify(const string& path) const;
	bool match(const HashSums& other) const;
	bool empty() const;

	void fill(const string& path);

	static string getStringHash(const Type& type, const string& pattern);
};

}

#endif

