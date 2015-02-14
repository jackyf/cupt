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

#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/binaryversion.hpp>

#include <internal/versionparse.hpp>

namespace cupt {
namespace cache {

BinaryPackage::BinaryPackage()
	: Package()
{}

unique_ptr< Version > BinaryPackage::_parse_version(const internal::VersionParseParameters& initParams) const
{
	return internal::parseBinaryVersion(initParams);
}

bool BinaryPackage::_is_architecture_appropriate(const string& binaryArchitecture, const Version* version) const
{
	auto binaryVersion = static_cast< const BinaryVersion* >(version);
	if (binaryVersion->isInstalled())
	{
		return true;
	}
	auto architecture = binaryVersion->architecture;
	return (architecture == "all" || architecture == binaryArchitecture);
}

vector< const BinaryVersion* > BinaryPackage::getVersions() const
{
	const auto& source = _get_versions();
	vector< const BinaryVersion* > result;
	FORIT(it, source)
	{
		result.push_back(static_cast< const BinaryVersion* >(it->get()));
	}
	return result;
}

const BinaryVersion* BinaryPackage::getInstalledVersion() const
{
	const auto& source = _get_versions();
	if (!source.empty())
	{
		// here we rely on the fact that installed version (if exists) adds first to the cache/package
		auto binaryVersion = static_cast< const BinaryVersion* >(source[0].get());
		if (binaryVersion->isInstalled())
		{
			return binaryVersion;
		}
	}

	return nullptr;
}

auto BinaryPackage::begin() const -> iterator
{
	return iterator(_get_versions().begin());
}

auto BinaryPackage::end() const -> iterator
{
	return iterator(_get_versions().end());
}

}
}

