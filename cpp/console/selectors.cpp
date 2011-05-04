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
typedef std::function< vector< string > () > __package_names_fetcher;

shared_ptr< const BinaryPackage > getBinaryPackage(shared_ptr< const Cache > cache, const string& packageName, bool throwOnError)
{
	shared_ptr< const BinaryPackage > result = cache->getBinaryPackage(packageName);
	if (!result && throwOnError)
	{
		fatal("unable to find the binary package '%s'", packageName.c_str());
	}
	return result;
}

shared_ptr< const SourcePackage > getSourcePackage(shared_ptr< const Cache > cache, const string& packageName, bool throwOnError)
{
	shared_ptr< const SourcePackage > result = cache->getSourcePackage(packageName);
	if (!result && throwOnError)
	{
		fatal("unable to find the source package '%s'", packageName.c_str());
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
			fatal("unable to find version '%s' for package '%s'", versionString.c_str(), packageName.c_str());
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
				fatal("bad distribution '%s' requested, use archive or codename", distributionExpression.c_str());
			}
			else
			{
				warn("bad distribution '%s' requested, use archive or codename", distributionExpression.c_str());
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
				fatal("cannot find distribution '%s' for package '%s'",
						distributionExpression.c_str(), packageName.c_str());
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
			fatal("for the package '%s' and the distribution '%s' several versions found: %s;"
					" you should explicitly select by version", packageName.c_str(),
					distributionExpression.c_str(), join(", ", versionStrings).c_str());
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
			fatal("no versions available for package '%s'", packageName.c_str());
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
		fatal("unable to find appropriate source or binary version for '%s'", packageExpression.c_str());
	}
	return sourceVersion;
}

vector< shared_ptr< const Version > > __select_versions_wildcarded(shared_ptr< const Cache > cache,
		const string& packageExpression, __version_selector versionSelector,
		__package_names_fetcher packageNamesFetcher, bool throwOnError)
{
	static sregex packageAndRemainderRegex = sregex::compile("([^=/]+)((?:=|/).*)?");

	smatch m;
	if (!regex_match(packageExpression, m, packageAndRemainderRegex))
	{
		fatal("bad package name in package expression '%s'", packageExpression.c_str());
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
		const char* packageNameGlob = packageNameExpression.c_str();

		auto packageNames = packageNamesFetcher();
		FORIT(proposedPackageNameIt, packageNames)
		{
			const string& proposedPackageName = *proposedPackageNameIt;
			if (!fnmatch(packageNameGlob, proposedPackageName.c_str(), 0))
			{
				auto version = versionSelector(cache, proposedPackageName + remainder, false);
				if (version)
				{
					result.push_back(version);
				}
			}
		}

		if (result.empty() && throwOnError)
		{
			fatal("no appropriate versions available for wildcarded version expression '%s'", packageExpression.c_str());
		}
	}

	return result;
}

vector< shared_ptr< const BinaryVersion > > selectBinaryVersionsWildcarded(shared_ptr< const Cache > cache,
		const string& packageExpression, bool throwOnError)
{
	static auto packageNamesFetcher = [&cache]() -> vector< string >
	{
		return cache->getBinaryPackageNames();
	};
	static auto versionSelector =
			[](shared_ptr< const Cache > cache, const string& packageName, bool throwOnError) -> shared_ptr< const Version >
			{
				return static_pointer_cast< const Version >(selectBinaryVersion(cache, packageName, throwOnError));
			};

	auto source = __select_versions_wildcarded(cache, packageExpression, versionSelector,
			packageNamesFetcher, throwOnError);

	vector< shared_ptr< const BinaryVersion > > result;
	for (size_t i = 0; i < source.size(); ++i)
	{
		auto version = dynamic_pointer_cast< const BinaryVersion >(source[i]);
		if (!version)
		{
			fatal("internal error: version is not binary");
		}
		result.push_back(version);
	}

	return result;
}

vector< shared_ptr< const SourceVersion > > selectSourceVersionsWildcarded(shared_ptr< const Cache > cache,
		const string& packageExpression, bool throwOnError)
{
	static auto packageNamesFetcher = [&cache]() -> vector< string >
	{
		return cache->getSourcePackageNames();
	};
	static auto versionSelector =
			[](shared_ptr< const Cache > cache, const string& packageName, bool throwOnError) -> shared_ptr< const Version >
			{
				return static_pointer_cast< const Version >(selectSourceVersion(cache, packageName, throwOnError));
			};

	auto source = __select_versions_wildcarded(cache, packageExpression, versionSelector,
			packageNamesFetcher, throwOnError);

	vector< shared_ptr< const SourceVersion > > result;
	for (size_t i = 0; i < source.size(); ++i)
	{
		auto version = dynamic_pointer_cast< const SourceVersion >(source[i]);
		if (!version)
		{
			fatal("internal error: version is not source");
		}
		result.push_back(version);
	}

	return result;
}
