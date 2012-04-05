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

bool Package::memoize = false;

Package::Package(const shared_ptr< const string >& binaryArchitecture)
	: __parsed_versions(NULL), _binary_architecture(binaryArchitecture)
{}

void Package::addEntry(const Version::InitializationParameters& initParams)
{
	__unparsed_versions.push_back(initParams);
}

vector< shared_ptr< Version > > Package::_get_versions() const
{
	if (! __parsed_versions)
	{
		// versions were either not parsed or parsed, but not saved
		vector< shared_ptr< Version > > result;

		vector< Version::InitializationParameters > newUnparsedVersions;
		FORIT(unparsedVersionIt, __unparsed_versions)
		{
			Version::InitializationParameters& initParams = *unparsedVersionIt;
			try
			{
				__merge_version(_parse_version(initParams), result);
				if (!memoize)
				{
					newUnparsedVersions.push_back(initParams);
				}
			}
			catch (Exception& e)
			{
				warn2(__("error while parsing a version for the package '%s'"), initParams.packageName);
			}
		}
		if (result.empty())
		{
			warn2(__("no valid versions available, discarding the package"));
		}
		__unparsed_versions.swap(newUnparsedVersions);

		if (memoize)
		{
			__parsed_versions = new vector< shared_ptr< Version > >();
			__parsed_versions->swap(result);
			return *__parsed_versions;
		}
		else
		{
			return result;
		}
	}
	else
	{
		return *__parsed_versions;
	}
}

vector< shared_ptr< const Version > > Package::getVersions() const
{
	auto source = _get_versions();
	vector< shared_ptr< const Version > > result;
	std::copy(source.begin(), source.end(), std::back_inserter(result));
	return result;
}

void Package::__merge_version(shared_ptr< Version >&& parsedVersion, vector< shared_ptr< Version > >& result) const
{
	if (!_is_architecture_appropriate(parsedVersion))
	{
		return; // skip this version
	}

	// merging
	try
	{
		const auto& parsedVersionString = parsedVersion->versionString;
		auto foundItem = std::find_if(result.begin(), result.end(), [&parsedVersionString](shared_ptr< const Version > elem) -> bool
		{
			return (elem->versionString == parsedVersionString);
		});

		if (foundItem == result.end())
		{
			// no such version before, just add it
			result.push_back(std::move(parsedVersion));
		}
		else
		{
			// there is such version string
			const auto& foundVersion = *foundItem;

			auto binaryVersion = dynamic_pointer_cast< BinaryVersion >(foundVersion);
			if ((binaryVersion && binaryVersion->isInstalled()) || foundVersion->areHashesEqual(parsedVersion))
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
					shared_ptr< BinaryVersion > binaryParsedVersion =
							dynamic_pointer_cast< BinaryVersion >(parsedVersion);
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

shared_ptr< const Version > Package::getSpecificVersion(const string& versionString) const
{
	auto source = _get_versions();
	FORIT(versionIt, source)
	{
		if ((*versionIt)->versionString == versionString)
		{
			return *versionIt;
		}
	}
	return shared_ptr< const Version >();
}

Package::~Package()
{
	delete __parsed_versions;
}

}
}

