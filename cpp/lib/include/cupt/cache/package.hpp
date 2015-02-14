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
namespace internal {

struct VersionParseParameters;

template< typename VersionType >
class CUPT_API BasePackageIterator: public std::iterator< std::bidirectional_iterator_tag, const VersionType* >
{
	friend class cache::Package;
	friend class cache::BinaryPackage;
	friend class cache::SourcePackage;

	typedef vector< unique_ptr< cache::Version > >::const_iterator UnderlyingIterator;

	UnderlyingIterator __ui;

	BasePackageIterator(UnderlyingIterator);
 public:
	typedef BasePackageIterator Self;

	Self& operator++();
	const VersionType* operator*() const;
	bool operator==(const Self&) const;
	bool operator!=(const Self&) const;
};

}

namespace cache {

/// a container for all versions of the same package name
class CUPT_API Package
{
	vector< unique_ptr< Version > > __parsed_versions;

	CUPT_LOCAL void __merge_version(const string&, unique_ptr< Version >&&);

	Package(const Package&);
	Package& operator=(const Package&);
 protected:
	/// @cond
	;

	CUPT_LOCAL const vector< unique_ptr< Version > >& _get_versions() const;
	CUPT_LOCAL virtual unique_ptr< Version > _parse_version(const internal::VersionParseParameters&) const = 0;
	CUPT_LOCAL virtual bool _is_architecture_appropriate(const string&, const Version*) const = 0;
	/// @endcond
 public:
	/// constructor
	Package();
	/// destructor
	virtual ~Package();
	/// @cond
	CUPT_LOCAL void addEntry(const internal::VersionParseParameters&);
	/// @endcond

	/// gets list of versions
	vector< const Version* > getVersions() const;
	/// gets version with a certain Version::versionString
	/**
	 * @return version if found, empty pointer if not found
	 * @param versionString version string
	 */
	const Version* getSpecificVersion(const string& versionString) const;

	typedef internal::BasePackageIterator< Version > iterator;
	iterator begin() const;
	iterator end() const;
};

}
}

#endif

