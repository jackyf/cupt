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

#include <cupt/fwd.hpp>
#include <cupt/cache/package.hpp>

namespace cupt {
namespace cache {

class BinaryPackage: public Package
{
	const bool __allow_reinstall;
 protected:
	virtual shared_ptr< Version > _parse_version(const Version::InitializationParameters& initParams) const;
	virtual bool _is_architecture_appropriate(const shared_ptr< const Version >&) const;
 public:
	BinaryPackage(const shared_ptr< const string >& binaryArchitecture, bool allowReinstall);
	vector< shared_ptr< const BinaryVersion > > getVersions() const;
	shared_ptr< const BinaryVersion > getInstalledVersion() const;
};

}
}

#endif

