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

int search(Context& context)
{
	auto config = context.getConfig();
	vector< string > patterns;

	if (!shellMode)
	{
		BinaryVersion::parseRelations = false;
	}

	bpo::options_description options;
	options.add_options()
		("names-only,n", "")
		("case-sensitive", "")
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

	auto cache = context.getCache(/* source */ false, /* binary */ variables.count("installed-only") == 0,
			/* installed */ true);

	int regexFlags = regex_constants::ECMAScript | regex_constants::optimize;
	if (variables.count("case-sensitive") == 0)
	{
		regexFlags |= regex_constants::icase;
	}
	vector< sregex > regexes;
	std::for_each(patterns.begin(), patterns.end(), [&regexes, &regexFlags](const string& pattern)
	{
		try
		{
			regexes.push_back(sregex::compile(pattern.c_str(), regex_constants::syntax_option_type(regexFlags)));
		}
		catch (regex_error&)
		{
			fatal2(__("invalid regular expression '%s'"), pattern);
		}
	});

	smatch m;

	vector< string > packageNames = cache->getBinaryPackageNames();
	std::sort(packageNames.begin(), packageNames.end());
	if (config->getBool("apt::cache::namesonly"))
	{
		// search only in package names
		FORIT(packageNameIt, packageNames)
		{
			const string& packageName = *packageNameIt;
			bool matched = true;
			FORIT(regexIt, regexes)
			{
				if (!regex_search(packageName, m, *regexIt))
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
	else
	{
		FORIT(packageNameIt, packageNames)
		{
			const string& packageName = *packageNameIt;

			auto package = cache->getBinaryPackage(packageName);
			auto versions = package->getVersions();

			set< string > printedShortDescriptions;
			for (const auto& v: versions)
			{
				bool matched = true;

				auto description = cache->getLocalizedDescription(v);

				FORIT(regexIt, regexes)
				{
					const sregex& regex = *regexIt;
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
					auto shortDescription = description.substr(0, description.find('\n'));
					if (printedShortDescriptions.insert(shortDescription).second)
					{
						cout << packageName << " - " << shortDescription << endl;
					}
				}
			}
		}
	}

	return 0;
}

