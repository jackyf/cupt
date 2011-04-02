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

	Package(const Package&);
	Package& operator=(const Package&);
 protected:
	/// @cond
	shared_ptr< const string > _binary_architecture;

	vector< shared_ptr< Version > > _get_versions() const;
	virtual shared_ptr< Version > _parse_version(const Version::InitializationParameters&) const = 0;
	virtual bool _is_architecture_appropriate(const shared_ptr< const Version >&) const = 0;
	/// @endcond
 public:
	/// constructor
	/**
	 * @param binaryArchitecture binary architecture of the system
	 */
	Package(const shared_ptr< const string >& binaryArchitecture);
	/// destructor
	virtual ~Package();
	/// adds new element (version initialization parameters) to the container
	void addEntry(const Version::InitializationParameters&);
	/// gets list of versions
	vector< shared_ptr< const Version > > getVersions() const;
	/// gets version with a certain Version::versionString
	/**
	 * @return version if found, empty pointer if not found
	 * @param versionString version string
	 */
	shared_ptr< const Version > getSpecificVersion(const string& versionString) const;

	/// memoize parsed versions
	static bool memoize;
};

}
}

#endif

