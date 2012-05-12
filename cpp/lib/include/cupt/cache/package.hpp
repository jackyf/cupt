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
class CUPT_API Package
{
	vector< unique_ptr< Version > > __parsed_versions;

	CUPT_LOCAL void __merge_version(unique_ptr< Version >&&);

	Package(const Package&);
	Package& operator=(const Package&);
 protected:
	/// @cond
	const string* _binary_architecture;

	CUPT_LOCAL const vector< unique_ptr< Version > >& _get_versions() const;
	CUPT_LOCAL virtual unique_ptr< Version > _parse_version(const Version::InitializationParameters&) const = 0;
	CUPT_LOCAL virtual bool _is_architecture_appropriate(const Version*) const = 0;
	/// @endcond
 public:
	/// constructor
	/**
	 * @param binaryArchitecture binary architecture of the system
	 */
	Package(const string*);
	/// destructor
	virtual ~Package();
	/// adds new element (version initialization parameters) to the container
	void addEntry(const Version::InitializationParameters&);
	/// gets list of versions
	vector< const Version* > getVersions() const;
	/// gets version with a certain Version::versionString
	/**
	 * @return version if found, empty pointer if not found
	 * @param versionString version string
	 */
	const Version* getSpecificVersion(const string& versionString) const;
};

}
}

#endif

