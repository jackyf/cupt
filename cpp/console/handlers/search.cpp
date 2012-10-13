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
#include <iostream>
using std::cout;
using std::endl;

#include <common/regex.hpp>

#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/binaryversion.hpp>

#include "../common.hpp"
#include "../misc.hpp"
#include "../handlers.hpp"
#include "../functionselectors.hpp"

namespace {

vector< sregex > generateSearchRegexes(const vector< string >& patterns, bool caseSensitive)
{
	int regexFlags = regex_constants::ECMAScript | regex_constants::optimize;
	if (!caseSensitive)
	{
		regexFlags |= regex_constants::icase;
	}

	vector< sregex > result;

	for (const string& pattern: patterns)
	{
		try
		{
			result.push_back(sregex::compile(pattern.c_str(), regex_constants::syntax_option_type(regexFlags)));
		}
		catch (regex_error&)
		{
			fatal2(__("invalid regular expression '%s'"), pattern);
		}
	};

	return result;
}

void searchInPackageNames(const vector< string >& packageNames,
		const vector< sregex >& regexes, smatch& m)
{
	for (const auto& packageName: packageNames)
	{
		bool matched = true;
		for (const auto& regex: regexes)
		{
			if (!regex_search(packageName, m, regex))
			{
				matched = false;
				break;
			}
		}
		if (matched)
		{
			cout << packageName << endl;
		}
	}
}

inline string getShortDescription(const string& description)
{
	return description.substr(0, description.find('\n'));
}

void searchInPackageNamesAndDescriptions(const Cache& cache, const vector< string >& packageNames,
		const vector< sregex >& regexes, smatch& m)
{
	for (const string& packageName: packageNames)
	{
		auto package = cache.getBinaryPackage(packageName);

		set< string > printedShortDescriptions;
		for (const auto& v: *package)
		{
			bool matched = true;

			auto description = cache.getLocalizedDescription(v);

			for (const sregex& regex: regexes)
			{
				if (regex_search(packageName, m, regex))
				{
					continue;
				}
				if (regex_search(description, m, regex))
				{
					continue;
				}
				matched = false;
				break;
			}

			if (matched)
			{
				auto shortDescription = getShortDescription(description);
				if (printedShortDescriptions.insert(shortDescription).second)
				{
					cout << packageName << " - " << shortDescription << endl;
				}
			}
		}
	}
}

void searchByFSE(const Cache& cache, vector< string >& patterns)
{
	string fse = patterns[0];

	patterns.erase(patterns.begin());
	checkNoExtraArguments(patterns);

	auto functionalQuery = FunctionalSelector::parseQuery(fse, true);
	auto&& foundVersions = FunctionalSelector(cache).selectBestVersions(*functionalQuery);
	for (const auto& version: foundVersions)
	{
		auto binaryVersion = static_cast< const BinaryVersion* >(version);
		cout << format2("%s - %s\n",
				binaryVersion->packageName,
				getShortDescription(binaryVersion->description));
	}
}

}

int search(Context& context)
{
	auto config = context.getConfig();
	vector< string > patterns;

	bpo::options_description options;
	options.add_options()
		("names-only,n", "")
		("case-sensitive", "")
		("fse,f", "")
		("installed-only", "");

	auto variables = parseOptions(context, options, patterns);

	if (variables.count("names-only"))
	{
		config->setScalar("apt::cache::namesonly", "yes");
	}
	if (!shellMode && config->getBool("apt::cache::namesonly"))
	{
		BinaryVersion::parseInfoOnly = false;
	}

	if (patterns.empty())
	{
		fatal2(__("no search patterns specified"));
	}

	if (variables.count("fse"))
	{
		auto cache = context.getCache(true, true, true);
		searchByFSE(*cache, patterns);
	}
	else
	{
		if (!shellMode)
		{
			BinaryVersion::parseRelations = false;
		}
		auto cache = context.getCache(/* source */ false,
				/* binary */ variables.count("installed-only") == 0,
				/* installed */ true);

		auto regexes = generateSearchRegexes(patterns, variables.count("case-sensitive"));
		smatch m;

		vector< string > packageNames = cache->getBinaryPackageNames();
		std::sort(packageNames.begin(), packageNames.end());
		if (config->getBool("apt::cache::namesonly"))
		{
			searchInPackageNames(packageNames, regexes, m);
		}
		else
		{
			searchInPackageNamesAndDescriptions(*cache, packageNames, regexes, m);
		}
	}

	return 0;
}

