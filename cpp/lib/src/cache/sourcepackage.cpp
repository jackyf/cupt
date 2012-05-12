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

#include <cupt/cache/sourcepackage.hpp>
#include <cupt/cache/sourceversion.hpp>

#include <internal/common.hpp>

namespace cupt {
namespace cache {

SourcePackage::SourcePackage(const string* binaryArchitecture)
	: Package(binaryArchitecture)
{}

unique_ptr< Version > SourcePackage::_parse_version(const Version::InitializationParameters& initParams) const
{
	return unique_ptr< Version >(SourceVersion::parseFromFile(initParams));
}

bool SourcePackage::_is_architecture_appropriate(const Version* version) const
{
	const vector< string >& architectures = static_cast< const SourceVersion* >(version)->architectures;
	FORIT(architectureIt, architectures)
	{
		if(*architectureIt == "all" || internal::architectureMatch(*_binary_architecture, *architectureIt))
		{
			return true;
		}
	}
	return false;
}

vector< const SourceVersion* > SourcePackage::getVersions() const
{
	const auto& source = _get_versions();
	vector< const SourceVersion* > result;
	FORIT(it, source)
	{
		result.push_back(static_cast< const SourceVersion* >(it->get()));
	}
	return result;
}

}
}

