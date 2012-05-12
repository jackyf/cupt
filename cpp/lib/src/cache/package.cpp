/**************************************************************************
*   Copyright (C) 2010-2011 by Eugene V. Lyubimkin                        *
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

namespace cupt {
namespace cache {

Package::Package(const string* binaryArchitecture)
	: _binary_architecture(binaryArchitecture)
{}

void Package::addEntry(const Version::InitializationParameters& initParams)
{
	try
	{
		__merge_version(_parse_version(initParams));
	}
	catch (Exception& e)
	{
		warn2(__("error while parsing a version for the package '%s'"), initParams.packageName);
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

void Package::__merge_version(unique_ptr< Version >&& parsedVersion)
{
	if (!_is_architecture_appropriate(parsedVersion.get()))
	{
		return; // skip this version
	}

	// merging
	try
	{
		const auto& parsedVersionString = parsedVersion->versionString;
		auto foundItem = std::find_if(__parsed_versions.begin(), __parsed_versions.end(),
				[&parsedVersionString](const unique_ptr< Version >& elem) -> bool
				{
					return (elem->versionString == parsedVersionString);
				});

		if (foundItem == __parsed_versions.end())
		{
			// no such version before, just add it
			__parsed_versions.push_back(std::move(parsedVersion));
		}
		else
		{
			// there is such version string
			const auto& foundVersion = *foundItem;

			auto binaryVersion = dynamic_cast< BinaryVersion* >(foundVersion.get());
			if ((binaryVersion && binaryVersion->isInstalled()) || foundVersion->areHashesEqual(parsedVersion.get()))
			{
				/*
				1)
				this is installed version
				as dpkg now doesn't provide hash sums, let's assume that
				local version is the same that available from archive
				2)
				ok, this is the same version
				*/

				// so, adding new Version::Source info
				foundVersion->sources.push_back(parsedVersion->sources[0]);

				if (binaryVersion && binaryVersion->isInstalled())
				{
					BinaryVersion* binaryParsedVersion =
							dynamic_cast< BinaryVersion* >(parsedVersion.get());
					binaryVersion->file.hashSums = binaryParsedVersion->file.hashSums;
				}
			}
			else
			{
				// err, no, this is different version :(
				vector< string > foundOrigins;
				for (const auto& foundSource: foundVersion->sources)
				{
					foundOrigins.emplace_back(foundSource.release->baseUri);
				}
				warn2(__("discarding a duplicate version with different hash sums: package: '%s', "
						"version: '%s', origin of discarded version: '%s', origins left: '%s'"),
						parsedVersion->packageName, parsedVersion->versionString,
						parsedVersion->sources[0].release->baseUri, join(", ", foundOrigins));
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
	FORIT(versionIt, source)
	{
		if ((*versionIt)->versionString == versionString)
		{
			return versionIt->get();
		}
	}
	return nullptr;
}

Package::~Package()
{}

}
}

