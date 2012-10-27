/**************************************************************************
*   Copyright (C) 2012 by Eugene V. Lyubimkin                             *
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
#ifndef CUPT_PACKAGE_ID_SEEN
#define CUPT_PACKAGE_ID_SEEN

namespace cupt {

class CUPT_API PackageId
{
	uint32_t __id;
 public:
	PackageId();
	explicit PackageId(const char* start, size_t len);
	explicit PackageId(const string&);
	const string& name() const;
	uint32_t rawId() const;

	bool operator==(const PackageId& other) const { return __id == other.__id; }
	bool operator<(const PackageId& other) const { return __id < other.__id; }
	operator bool() const { return __id; }

	static bool compareByName(PackageId left, PackageId right) { return left.name() < right.name(); }
};

}

namespace std {

template <>
struct CUPT_API hash< cupt::PackageId >
{
	size_t operator()(cupt::PackageId packageId) const
	{
		return packageId.rawId();
	}
};

}

#endif

