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
#ifndef CUPT_CACHE_SOURCEPACKAGE_SEEN
#define CUPT_CACHE_SOURCEPACKAGE_SEEN

/// @file

#include <cupt/fwd.hpp>
#include <cupt/cache/package.hpp>

namespace cupt {
namespace cache {

/// package for source versions
class CUPT_API SourcePackage: public Package
{
 protected:
	/// @cond
	CUPT_LOCAL virtual unique_ptr< Version > _parse_version(const Version::InitializationParameters& initParams) const;
	CUPT_LOCAL virtual bool _is_architecture_appropriate(const Version*) const;
	/// @endcond
 public:
	/// constructor
	/**
	 * @param binaryArchitecture system binary architecture
	 */
	SourcePackage(const string* binaryArchitecture);
	vector< const SourceVersion* > getVersions() const;
};

}
}

#endif

