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
#ifndef CUPT_CACHE_PACKAGE_SEEN
#define CUPT_CACHE_PACKAGE_SEEN

/// @file

#include <cupt/cache/version.hpp>

namespace cupt {
namespace cache {

/// a container for all versions of the same package name
class Package
{
	mutable vector< Version::InitializationParameters > __unparsed_versions;
	mutable vector< shared_ptr< Version > >* __parsed_versions;

	void __merge_version(const shared_ptr< Version >&, vector< shared_ptr< Version > >& result) const;
 protected:
	shared_ptr< const string > _binary_architecture;

	vector< shared_ptr< Version > > _get_versions() const;
	virtual shared_ptr< Version > _parse_version(const Version::InitializationParameters&) const = 0;
	virtual bool _is_architecture_appropriate(const shared_ptr< const Version >&) const = 0;

 public:
	Package(const shared_ptr< const string >& binaryArchitecture);
	virtual ~Package();
	void addEntry(const Version::InitializationParameters&);
	vector< shared_ptr< const Version > > getVersions() const;
	shared_ptr< const Version > getSpecificVersion(const string& versionString) const;

	static bool memoize;
};

}
}

#endif

