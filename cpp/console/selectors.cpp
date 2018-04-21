/**************************************************************************
*   Copyright (C) 2010-2013 by Eugene V. Lyubimkin                        *
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

#include <functional>

#include <common/regex.hpp>
#include <cupt/versionstring.hpp>

#include "common.hpp"
#include "selectors.hpp"
#include "functionselectors.hpp"

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

string convertWildcardedExpressionToFse(const string& expression)
{
	auto getPackageNameFse = [](const string& packageNameExpression)
	{
		return format2("package:name(%s)", globToRegexString(packageNameExpression));
	};

	auto delimiterPosition = expression.find_first_of("/=");
	if (delimiterPosition != string::npos)
	{
		string additionalFseRule;

		string packageGlobExpression = expression.substr(0, delimiterPosition);
		string remainder = expression.substr(delimiterPosition+1);
		if (expression[delimiterPosition] == '/') // distribution
		{
			static const sregex distributionExpressionRegex = sregex::compile("[a-z-]+");
			static smatch m;
			if (!regex_match(remainder, m, distributionExpressionRegex))
			{
				fatal2(__("bad distribution '%s' requested, use archive or codename"), remainder);
			}

			additionalFseRule = format2("or(release:archive(%s), release:codename(%s))", remainder, remainder);
		}
		else // exact version string
		{
			checkVersionString(remainder);
			additionalFseRule = format2("version(%s)", globToRegexString(remainder));
		}

		return format2("%s & %s", getPackageNameFse(packageGlobExpression), additionalFseRule);
	}
	else
	{
		return getPackageNameFse(expression);
	}
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
		const string& expression, QueryProcessor queryProcessor, bool binary, bool throwOnEmptyResult)
{
	FunctionalSelector selector(cache);
	auto query = selector.parseQuery(expression, binary);

	auto result = __convert_version_type< VersionType >((selector.*queryProcessor)(*query));
	if (throwOnEmptyResult && result.empty())
	{
		fatal2(__("the function expression '%s' selected nothing"), expression);
	}
	return std::move(result);
}

string getFse(const string& expression)
{
	if (isFunctionExpression(expression))
	{
		return expression; // already is FSE
	}
	else
	{
		return convertWildcardedExpressionToFse(expression);
	}
}

template < typename QueryProcessor >
vector< const SourceVersion* > selectBySourceFse(const Cache& cache,
		const string& fse, QueryProcessor queryProcessor, bool throwOnError)
{
	auto result = __select_using_function< SourceVersion >(cache, fse, queryProcessor, false, false);
	if (result.empty())
	{
		string binaryFromSourceFse = format2("binary-to-source(%s)", fse);
		result = __select_using_function< SourceVersion >(cache, binaryFromSourceFse, queryProcessor, false, throwOnError);
	}
	return result;
}

vector< const BinaryVersion* > selectBinaryVersionsWildcarded(const Cache& cache,
		const string& packageExpression, bool throwOnError)
{
	return __select_using_function< BinaryVersion >(cache, getFse(packageExpression),
			&FunctionalSelector::selectBestVersions, true, throwOnError);
}

vector< const SourceVersion* > selectSourceVersionsWildcarded(const Cache& cache,
		const string& packageExpression, bool throwOnError)
{
	return selectBySourceFse(cache, getFse(packageExpression),
			&FunctionalSelector::selectBestVersions, throwOnError);
}

vector< const BinaryVersion* > selectAllBinaryVersionsWildcarded(const Cache& cache,
		const string& packageExpression, bool throwOnError)
{
	return __select_using_function< BinaryVersion >(cache, getFse(packageExpression),
			&FunctionalSelector::selectAllVersions, true, throwOnError);
}

vector< const SourceVersion* > selectAllSourceVersionsWildcarded(const Cache& cache,
		const string& packageExpression, bool throwOnError)
{
	return selectBySourceFse(cache, getFse(packageExpression),
			&FunctionalSelector::selectAllVersions, throwOnError);
}

