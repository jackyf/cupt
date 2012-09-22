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
#include "functionselectors.hpp"

typedef std::function< vector< string > (const Cache&) > __package_names_fetcher;

const BinaryPackage* getBinaryPackage(const Cache& cache, const string& packageName, bool throwOnError)
{
	auto result = cache.getBinaryPackage(packageName);
	if (!result && throwOnError)
	{
		fatal2(__("unable to find the binary package '%s'"), packageName);
	}
	return result;
}

const SourcePackage* getSourcePackage(const Cache& cache, const string& packageName, bool throwOnError)
{
	auto result = cache.getSourcePackage(packageName);
	if (!result && throwOnError)
	{
		fatal2(__("unable to find the source package '%s'"), packageName);
	}
	return result;
}

template < typename PackageSelector >
const Version* __select_version(const Cache& cache,
		const string& packageExpression, PackageSelector packageSelector, bool throwOnError)
{
	typedef const Version* ReturnType;

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
			fatal2(__("unable to find the version '%s' for the package '%s'"), versionString, packageName);
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
				fatal2(__("bad distribution '%s' requested, use archive or codename"), distributionExpression);
			}
			else
			{
				warn2(__("bad distribution '%s' requested, use archive or codename"), distributionExpression);
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
				fatal2(__("cannot find the distribution '%s' for the package '%s'"),
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
			fatal2(__("several versions found for the package '%s' and the distribution '%s': %s; "
					" you should explicitly select by version"), packageName,
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
		auto version = cache.getPolicyVersion(package);
		if (!version && throwOnError)
		{
			fatal2(__("no versions available for the package '%s'"), packageName);
		}
		return version;
	}
}

const BinaryVersion* selectBinaryVersion(const Cache& cache,
		const string& packageExpression, bool throwOnError)
{
	return static_cast< const BinaryVersion* >(__select_version< decltype(getBinaryPackage) >
			(cache, packageExpression, getBinaryPackage, throwOnError));
}

const SourceVersion* selectSourceVersion(const Cache& cache,
		const string& packageExpression, bool throwOnError)
{
	auto sourceVersion = static_cast< const SourceVersion* >(__select_version< decltype(getSourcePackage) >
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
		return static_cast< const SourceVersion* >(__select_version< decltype(getSourcePackage) >
				(cache, newPackageExpression, getSourcePackage, throwOnError));
	}
	else if (throwOnError)
	{
		fatal2(__("unable to find an appropriate source or binary version for '%s'"), packageExpression);
	}
	return sourceVersion;
}

static vector< string > __select_package_names_wildcarded(const Cache& cache,
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

template < typename VersionType, typename VersionSelector >
vector< const VersionType* > __select_versions_wildcarded(const Cache& cache,
		const string& packageExpression, VersionSelector versionSelector,
		__package_names_fetcher packageNamesFetcher, bool throwOnError)
{
	static sregex packageAndRemainderRegex = sregex::compile("([^=/]+)((?:=|/).*)?");

	smatch m;
	if (!regex_match(packageExpression, m, packageAndRemainderRegex))
	{
		fatal2(__("bad package name in the package expression '%s'"), packageExpression);
	}
	string packageNameExpression = m[1];
	string remainder;
	if (m[2].matched)
	{
		remainder = m[2];
	}

	vector< const VersionType* > result;
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
			fatal2(__("no appropriate versions available for the wildcarded version expression '%s'"), packageExpression);
		}
	}

	return result;
}

static vector< string > getBinaryPackageNames(const Cache& cache)
{
	return cache.getBinaryPackageNames();
}

bool isFunctionExpression(const string& expression)
{
	return (expression.find_first_of("()") != string::npos);
}

template < typename VersionType >
static vector< const VersionType* > __convert_version_type(list< const Version* >&& source)
{
	vector< const VersionType* > result;
	for (const auto& oldVersion: source)
	{
		auto version = dynamic_cast< const VersionType* >(oldVersion);
		if (!version)
		{
			fatal2i("version has a wrong type");
		}
		result.push_back(version);
	}

	return result;
}

template < typename VersionType, typename QueryProcessor >
vector< const VersionType* > __select_using_function(const Cache& cache,
		const string& expression, QueryProcessor queryProcessor, bool binary, bool throwOnError)
{
	auto result = __convert_version_type< VersionType >(
				queryProcessor(cache, *parseFunctionQuery(expression, binary)));
	if (throwOnError && result.empty())
	{
		fatal2(__("the function expression '%s' selected nothing"), expression);
	}
	return std::move(result);
}

vector< const BinaryVersion* > selectBinaryVersionsWildcarded(const Cache& cache,
		const string& packageExpression, bool throwOnError)
{
	if (isFunctionExpression(packageExpression))
	{
		return __select_using_function< BinaryVersion >(cache, packageExpression,
				selectBestVersions, true, throwOnError);
	}
	else
	{
		return __select_versions_wildcarded< BinaryVersion >(cache, packageExpression,
				selectBinaryVersion, getBinaryPackageNames, throwOnError);
	}
}

static vector< string > getSourcePackageNames(const Cache& cache)
{
	return cache.getSourcePackageNames();
}

vector< const SourceVersion* > selectSourceVersionsWildcarded(const Cache& cache,
		const string& packageExpression, bool throwOnError)
{
	if (isFunctionExpression(packageExpression))
	{
		return __select_using_function< SourceVersion >(cache, packageExpression,
				selectBestVersions, false, throwOnError);
	}
	else
	{
		return __select_versions_wildcarded< SourceVersion >(cache, packageExpression,
				selectSourceVersion, getSourcePackageNames, throwOnError);
	}
}

template < typename VersionType, typename PackageSelector >
static vector< const VersionType* > __select_all_versions_wildcarded(
		const Cache& cache, const string& packageExpression,
		__package_names_fetcher packageNamesFetcher, PackageSelector packageSelector)
{
	vector< const VersionType* > result;

	auto packageNames = __select_package_names_wildcarded(cache, packageExpression, packageNamesFetcher);
	if (packageNames.empty())
	{
		fatal2(__("no packages available for the wildcarded expression '%s'"), packageExpression);
	}
	FORIT(packageNameIt, packageNames)
	{
		auto package = packageSelector(cache, *packageNameIt, false);
		auto versions = package->getVersions();
		std::move(versions.begin(), versions.end(), std::back_inserter(result));
	}

	return result;
}

vector< const BinaryVersion* > selectAllBinaryVersionsWildcarded(const Cache& cache,
		const string& packageExpression)
{
	if (isFunctionExpression(packageExpression))
	{
		return __select_using_function< BinaryVersion >(cache, packageExpression,
				selectAllVersions, true, false);
	}
	else
	{
		return __select_all_versions_wildcarded< BinaryVersion >(cache, packageExpression,
				getBinaryPackageNames, getBinaryPackage);
	}
}

vector< const SourceVersion* > selectAllSourceVersionsWildcarded(const Cache& cache,
		const string& packageExpression)
{
	if (isFunctionExpression(packageExpression))
	{
		return __select_using_function< SourceVersion >(cache, packageExpression,
				selectAllVersions, false, false);
	}
	else
	{
		return __select_all_versions_wildcarded< SourceVersion >(cache, packageExpression,
				getSourcePackageNames, getSourcePackage);
	}
}

