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
#ifndef CUPT_CACHE_BINARYPACKAGE_SEEN
#define CUPT_CACHE_BINARYPACKAGE_SEEN

/// @file

#include <cupt/fwd.hpp>
#include <cupt/cache/package.hpp>

namespace cupt {
namespace cache {

/// Package for binary versions
class CUPT_API BinaryPackage: public Package
{
	const bool __allow_reinstall;
 protected:
	/// @cond
	CUPT_LOCAL virtual unique_ptr< Version > _parse_version(const Version::InitializationParameters& initParams) const;
	CUPT_LOCAL virtual bool _is_architecture_appropriate(const Version*) const;
	/// @endcond
 public:
	/// constructor
	/**
	 * @param binaryArchitecture system binary architecture
	 * @param allowReinstall allow reinstalling installed version of this package,
	 * i.e. mangle the version string of installed version
	 */
	BinaryPackage(const string* binaryArchitecture, bool allowReinstall);
	/// gets list of versions
	vector< const BinaryVersion* > getVersions() const;
	/// gets installed version
	/**
	 * @return installed version if exists, empty pointer if not
	 */
	const BinaryVersion* getInstalledVersion() const;
};

}
}

#endif

