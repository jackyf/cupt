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

#include <fnmatch.h>

#include <functional>

#include <common/regex.hpp>

#include <cupt/cache/releaseinfo.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/sourcepackage.hpp>
#include <cupt/cache/binaryversion.hpp>
#include <cupt/cache/sourceversion.hpp>

#include "common.hpp"
#include "selectors.hpp"

typedef shared_ptr< const Version > (*__version_selector)(shared_ptr< const Cache >,
		const string& packageName, bool throwOnError);
typedef std::function< vector< string > (shared_ptr< const Cache >) > __package_names_fetcher;

shared_ptr< const BinaryPackage > getBinaryPackage(shared_ptr< const Cache > cache, const string& packageName, bool throwOnError)
{
	shared_ptr< const BinaryPackage > result = cache->getBinaryPackage(packageName);
	if (!result && throwOnError)
	{
		fatal2("unable to find the binary package '%s'", packageName);
	}
	return result;
}

shared_ptr< const SourcePackage > getSourcePackage(shared_ptr< const Cache > cache, const string& packageName, bool throwOnError)
{
	shared_ptr< const SourcePackage > result = cache->getSourcePackage(packageName);
	if (!result && throwOnError)
	{
		fatal2("unable to find the source package '%s'", packageName);
	}
	return result;
}

template < typename PackageSelector >
shared_ptr< const Version > __select_version(shared_ptr< const Cache > cache,
		const string& packageExpression, PackageSelector packageSelector, bool throwOnError)
{
	typedef shared_ptr< const Version > ReturnType;

	static const sregex exactVersionRegex = sregex::compile("(.*?)=(.*)");
	static const sregex exactDistributionRegex = sregex::compile("(.*?)/(.*)");
	smatch m;
	if (regex_match(packageExpression, m, exactVersionRegex))
	{
		// selecting by strict version string
		// example: "nlkt=0.3.2.1-1"
		string packageName = m[1];
		checkPackageName(packageName);
		string versionString = m[2];
		checkVersionString(versionString);

		auto package = packageSelector(cache, packageName, throwOnError);
		if (!package)
		{
			return ReturnType();
		}

		auto version = package->getSpecificVersion(versionString);

		if (!version && throwOnError)
		{
			fatal2("unable to find version '%s' for package '%s'", versionString, packageName);
		}
		return version;
	}
	else if (regex_match(packageExpression, m, exactDistributionRegex))
	{
		// selecting by release distibution
		string packageName = m[1];
		checkPackageName(packageName);

		string distributionExpression = m[2];

		static const sregex distributionExpressionRegex = sregex::compile("[a-z-]+");
		if (!regex_match(distributionExpression, m, distributionExpressionRegex))
		{
			if (throwOnError)
			{
				fatal2("bad distribution '%s' requested, use archive or codename", distributionExpression);
			}
			else
			{
				warn2("bad distribution '%s' requested, use archive or codename", distributionExpression);
				return ReturnType();
			}
		}

		auto package = packageSelector(cache, packageName, throwOnError);
		if (!package)
		{
			return ReturnType();
		}

		// example: "nlkt/sid" or "nlkt/unstable"
		auto versions = package->getVersions();
		decltype(versions) matchingVersions;
		FORIT(versionIt, versions)
		{
			FORIT(sourceIt, (*versionIt)->sources)
			{
				if (sourceIt->release->archive == distributionExpression ||
					sourceIt->release->codename == distributionExpression)
				{
					matchingVersions.push_back(*versionIt);
					break;
				}
			}
		}

		if (matchingVersions.empty())
		{
			// not found
			if (throwOnError)
			{
				fatal2("cannot find distribution '%s' for package '%s'",
						distributionExpression, packageName);
			}
			return ReturnType();
		}
		else if (matchingVersions.size() == 1)
		{
			return matchingVersions[0];
		}
		else
		{
			vector< string > versionStrings;
			FORIT(it, matchingVersions)
			{
				versionStrings.push_back((*it)->versionString);
			}
			fatal2("for the package '%s' and the distribution '%s' several versions found: %s;"
					" you should explicitly select by version", packageName,
					distributionExpression, join(", ", versionStrings));
			return ReturnType(); // unreachable
		}
	}
	else
	{
		const string& packageName = packageExpression;
		checkPackageName(packageName);

		auto package = packageSelector(cache, packageName, throwOnError);
		if (!package)
		{
			return ReturnType();
		}
		auto version = cache->getPolicyVersion(package);
		if (!version && throwOnError)
		{
			fatal2("no versions available for package '%s'", packageName);
		}
		return version;
	}
}

shared_ptr< const BinaryVersion > selectBinaryVersion(shared_ptr< const Cache > cache,
		const string& packageExpression, bool throwOnError)
{
	return static_pointer_cast< const BinaryVersion >(__select_version< decltype(getBinaryPackage) >
			(cache, packageExpression, getBinaryPackage, throwOnError));
}

shared_ptr< const SourceVersion > selectSourceVersion(shared_ptr< const Cache > cache,
		const string& packageExpression, bool throwOnError)
{
	auto sourceVersion = static_pointer_cast< const SourceVersion >(__select_version< decltype(getSourcePackage) >
			(cache, packageExpression, getSourcePackage, false));
	if (sourceVersion)
	{
		return sourceVersion;
	}

	auto binaryVersion = selectBinaryVersion(cache, packageExpression, false);
	if (binaryVersion)
	{
		auto newPackageExpression = binaryVersion->sourcePackageName +
				'=' + binaryVersion->sourceVersionString;
		return static_pointer_cast< const SourceVersion >(__select_version< decltype(getSourcePackage) >
				(cache, newPackageExpression, getSourcePackage, throwOnError));
	}
	else if (throwOnError)
	{
		fatal2("unable to find appropriate source or binary version for '%s'", packageExpression);
	}
	return sourceVersion;
}

static vector< string > __select_package_names_wildcarded(shared_ptr< const Cache > cache,
		const string& packageNameExpression, __package_names_fetcher packageNamesFetcher)
{
	vector< string > result = packageNamesFetcher(cache);

	auto notMatch = [&packageNameExpression, &cache](const string& packageName)
	{
		return fnmatch(packageNameExpression.c_str(), packageName.c_str(), 0);
	};
	result.erase(std::remove_if(result.begin(), result.end(), notMatch), result.end());

	return result;
}

vector< shared_ptr< const Version > > __select_versions_wildcarded(shared_ptr< const Cache > cache,
		const string& packageExpression, __version_selector versionSelector,
		__package_names_fetcher packageNamesFetcher, bool throwOnError)
{
	static sregex packageAndRemainderRegex = sregex::compile("([^=/]+)((?:=|/).*)?");

	smatch m;
	if (!regex_match(packageExpression, m, packageAndRemainderRegex))
	{
		fatal2("bad package name in package expression '%s'", packageExpression);
	}
	string packageNameExpression = m[1];
	string remainder;
	if (m[2].matched)
	{
		remainder = m[2];
	}

	vector< shared_ptr< const Version > > result;
	if (packageNameExpression.find('?') == string::npos && packageNameExpression.find('*') == string::npos)
	{
		// there are no wildcards
		auto version = versionSelector(cache, packageExpression, throwOnError);
		if (version)
		{
			result.push_back(version);
		}
	}
	else
	{
		// handling wildcards
		auto packageNames = __select_package_names_wildcarded(cache, packageNameExpression, packageNamesFetcher);
		FORIT(packageNameIt, packageNames)
		{
			auto version = versionSelector(cache, *packageNameIt + remainder, false);
			if (version)
			{
				result.push_back(version);
			}
		}

		if (result.empty() && throwOnError)
		{
			fatal2("no appropriate versions available for wildcarded version expression '%s'", packageExpression);
		}
	}

	return result;
}

static vector< string > getBinaryPackageNames(shared_ptr< const Cache > cache)
{
	return cache->getBinaryPackageNames();
}

vector< shared_ptr< const BinaryVersion > > selectBinaryVersionsWildcarded(shared_ptr< const Cache > cache,
		const string& packageExpression, bool throwOnError)
{
	auto versionSelector =
			[](shared_ptr< const Cache > cache, const string& packageName, bool throwOnError) -> shared_ptr< const Version >
			{
				return static_pointer_cast< const Version >(selectBinaryVersion(cache, packageName, throwOnError));
			};

	auto source = __select_versions_wildcarded(cache, packageExpression, versionSelector,
			getBinaryPackageNames, throwOnError);

	vector< shared_ptr< const BinaryVersion > > result;
	for (size_t i = 0; i < source.size(); ++i)
	{
		auto version = dynamic_pointer_cast< const BinaryVersion >(source[i]);
		if (!version)
		{
			fatal2i("version is not binary");
		}
		result.push_back(version);
	}

	return result;
}

static vector< string > getSourcePackageNames(shared_ptr< const Cache > cache)
{
	return cache->getSourcePackageNames();
}

vector< shared_ptr< const SourceVersion > > selectSourceVersionsWildcarded(shared_ptr< const Cache > cache,
		const string& packageExpression, bool throwOnError)
{
	auto versionSelector =
			[](shared_ptr< const Cache > cache, const string& packageName, bool throwOnError) -> shared_ptr< const Version >
			{
				return static_pointer_cast< const Version >(selectSourceVersion(cache, packageName, throwOnError));
			};

	auto source = __select_versions_wildcarded(cache, packageExpression, versionSelector,
			getSourcePackageNames, throwOnError);

	vector< shared_ptr< const SourceVersion > > result;
	for (size_t i = 0; i < source.size(); ++i)
	{
		auto version = dynamic_pointer_cast< const SourceVersion >(source[i]);
		if (!version)
		{
			fatal2i("version is not source");
		}
		result.push_back(version);
	}

	return result;
}

vector< shared_ptr< const BinaryVersion > > selectAllBinaryVersionsWildcarded(shared_ptr< const Cache > cache,
		const string& packageExpression)
{
	vector< shared_ptr< const BinaryVersion > > result;

	auto packageNames = __select_package_names_wildcarded(cache, packageExpression, getBinaryPackageNames);
	if (packageNames.empty())
	{
		fatal2("no binary packages available for the wildcarded expression '%s'", packageExpression);
	}
	FORIT(packageNameIt, packageNames)
	{
		auto package = getBinaryPackage(cache, *packageNameIt);
		auto versions = package->getVersions();
		std::move(versions.begin(), versions.end(), std::back_inserter(result));
	}

	return result;
}

vector< shared_ptr< const SourceVersion > > selectAllSourceVersionsWildcarded(shared_ptr< const Cache > cache,
		const string& packageExpression)
{
	vector< shared_ptr< const SourceVersion > > result;

	auto packageNames = __select_package_names_wildcarded(cache, packageExpression, getSourcePackageNames);
	if (packageNames.empty())
	{
		fatal2("no source packages available for the wildcarded expression '%s'", packageExpression);
	}
	FORIT(packageNameIt, packageNames)
	{
		auto package = getSourcePackage(cache, *packageNameIt);
		auto versions = package->getVersions();
		std::move(versions.begin(), versions.end(), std::back_inserter(result));
	}

	return result;
}

