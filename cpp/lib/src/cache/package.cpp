/**************************************************************************
*   Copyright (C) 2010-2012 by Eugene V. Lyubimkin                        *
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

#include <algorithm>
#include <cstring>

#include <cupt/cache/package.hpp>
#include <cupt/cache/releaseinfo.hpp>
#include <cupt/cache/binaryversion.hpp>
#include <cupt/versionstring.hpp>

#include <internal/versionparse.hpp>
#include <internal/common.hpp>

namespace cupt {
namespace cache {

Package::Package(const string* binaryArchitecture)
	: _binary_architecture(binaryArchitecture)
{}

void Package::addEntry(const internal::VersionParseParameters& initParams)
{
	try
	{
		__merge_version(_parse_version(initParams));
	}
	catch (Exception& e)
	{
		warn2(__("error while parsing a version for the package '%s'"), *initParams.packageNamePtr);
	}
}

const vector< unique_ptr< Version > >& Package::_get_versions() const
{
	return __parsed_versions;
}

vector< const Version* > Package::getVersions() const
{
	const auto& source = _get_versions();
	vector< const Version* > result;
	for (const auto& version: source)
	{
		result.push_back(version.get());
	}
	return result;
}

namespace {

inline bool __is_installed(const Version* version)
{
	auto binaryVersion = dynamic_cast< const BinaryVersion* >(version);
	return (binaryVersion && binaryVersion->isInstalled());
}

void addVersionIdSuffix(string* dest, const string& suffix)
{
	*dest += internal::idSuffixDelimiter;
	*dest += suffix;
}

const string installedSuffix = "installed";

}

void Package::__merge_version(unique_ptr< Version >&& parsedVersion)
{
	if (!_is_architecture_appropriate(parsedVersion.get()))
	{
		return; // skip this version
	}

	// merging
	try
	{
		if (__is_installed(parsedVersion.get()))
		{
			// no way to know is this version the same as in repositories,
			// until for example #667665 is implemented
			addVersionIdSuffix(&parsedVersion->versionString, installedSuffix);
			__parsed_versions.push_back(std::move(parsedVersion));
		}
		else
		{
			const auto& parsedVersionString = parsedVersion->versionString;

			bool clashed = false;
			bool merged = false;
			for (const auto& presentVersion: __parsed_versions)
			{
				if (!getOriginalVersionString(presentVersion->versionString).equal(parsedVersionString))
				{
					continue;
				}
				if (__is_installed(presentVersion.get()))
				{
					continue;
				}

				if (presentVersion->areHashesEqual(parsedVersion.get()))
				{
					// ok, this is the same version, so adding new Version::Source info
					presentVersion->sources.push_back(parsedVersion->sources[0]);
					merged = true;
					break;
				}
				else
				{
					clashed = true; // err, no, this is different version :(
				}
			}

			if (!merged)
			{
				if (clashed)
				{
					static size_t idCounter = 0;
					addVersionIdSuffix(&parsedVersion->versionString, format2("dhs%zu", idCounter++));
				}
				__parsed_versions.push_back(std::move(parsedVersion));
			}
		}
	}
	catch (Exception&)
	{
		fatal2(__("error while merging the version '%s' for the package '%s'"),
				parsedVersion->versionString, parsedVersion->packageName);
	};
}

const Version* Package::getSpecificVersion(const string& versionString) const
{
	const auto& source = _get_versions();
	for (const auto& version: source)
	{
		if (version->versionString == versionString)
		{
			return version.get();
		}
	}
	return nullptr;
}

auto Package::begin() const -> iterator
{
	return iterator(_get_versions().begin());
}

auto Package::end() const -> iterator
{
	return iterator(_get_versions().end());
}

Package::~Package()
{}

}
}

