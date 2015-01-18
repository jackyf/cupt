/**************************************************************************
*   Copyright (C) 2010-2015 by Eugene V. Lyubimkin                        *
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

#include "../handlers.hpp"
#include "../selectors.hpp"

#include <queue>
using std::queue;
#include <stack>
using std::stack;

using std::cout;
using std::endl;

struct PathEntry
{
	const BinaryVersion* version;
	BinaryVersion::RelationTypes::Type dependencyType;
	const RelationExpression* relationExpressionPtr;
};

struct VersionsAndLinks
{
	queue<const BinaryVersion*> versions;
	map<const BinaryVersion*, PathEntry> links;

	void addStartingVersion(const BinaryVersion* version)
	{
		versions.push(version);
		links[version]; // create empty PathEntry for version
	}

	void initialise(const Cache& cache, const vector<string>& arguments)
	{
		if (!arguments.empty())
		{
			// selected packages
			FORIT(argumentIt, arguments)
			{
				auto selectedVersions = selectBinaryVersionsWildcarded(cache, *argumentIt);
				FORIT(it, selectedVersions)
				{
					addStartingVersion(*it);
				}
			}
		}
		else
		{
			// the whole system
			for (const auto& installedVersion: cache.getInstalledVersions())
			{
				if (!cache.isAutomaticallyInstalled(installedVersion->packageName))
				{
					addStartingVersion(installedVersion);
				}
			}
		}
	}
};

int findDependencyChain(Context& context)
{
	// turn off info parsing, we don't need it, only relations
	if(!shellMode)
	{
		Version::parseInfoOnly = false;
	}

	vector< string > arguments;

	bpo::options_description options("");
	options.add_options()
		("installed-only", "");
	auto variables = parseOptions(context, options, arguments);

	if (arguments.empty())
	{
		fatal2(__("no binary package expressions specified"));
	}

	bool installedOnly = variables.count("installed-only") || (arguments.size() == 1);

	auto cache = context.getCache(/* source */ false, /* binary */ !installedOnly,
			/* installed */ true);

	auto leafPackageExpression = *(arguments.rbegin());
	arguments.erase(arguments.end() - 1);
	auto leafVersion = selectBinaryVersionsWildcarded(*cache, leafPackageExpression, true)[0];

	VersionsAndLinks val;
	val.initialise(*cache, arguments);

	auto config = context.getConfig();

	vector< BinaryVersion::RelationTypes::Type > relationGroups;
	relationGroups.push_back(BinaryVersion::RelationTypes::PreDepends);
	relationGroups.push_back(BinaryVersion::RelationTypes::Depends);
	if (config->getBool("cupt::resolver::keep-recommends"))
	{
		relationGroups.push_back(BinaryVersion::RelationTypes::Recommends);
	}
	if (config->getBool("cupt::resolver::keep-suggests"))
	{
		relationGroups.push_back(BinaryVersion::RelationTypes::Suggests);
	}

	while (!val.versions.empty())
	{
		auto version = val.versions.front();
		val.versions.pop();

		if (version == leafVersion)
		{
			// we found a path, re-walk it
			stack< PathEntry > path;
			const BinaryVersion* currentVersion = version;

			decltype(val.links.find(currentVersion)) it;
			while ((it = val.links.find(currentVersion)), it->second.version)
			{
				const PathEntry& pathEntry = it->second;
				path.push(pathEntry);
				currentVersion = pathEntry.version;
			}
			while (!path.empty())
			{
				PathEntry pathEntry = path.top();
				path.pop();
				cout << format2("%s %s: %s: %s",
						pathEntry.version->packageName, pathEntry.version->versionString,
						__(BinaryVersion::RelationTypes::strings[pathEntry.dependencyType].c_str()),
						pathEntry.relationExpressionPtr->toString()) << endl;
			}
			break;
		}

		FORIT(dependencyTypeIt, relationGroups)
		{
			auto dependencyType = *dependencyTypeIt;

			FORIT(relationExpressionIt, version->relations[dependencyType])
			{
				// insert recursive depends into queue
				auto satisfyingVersions = cache->getSatisfyingVersions(*relationExpressionIt);
				for (const auto& newVersion: satisfyingVersions)
				{
					auto insertResult = val.links.insert({ newVersion, PathEntry() });
					if (insertResult.second)
					{
						// new element
						PathEntry& newPathEntry = insertResult.first->second;
						newPathEntry.version = version;
						newPathEntry.dependencyType = dependencyType;
						// the pointer is valid because .version is alive
						newPathEntry.relationExpressionPtr = &*relationExpressionIt;

						val.versions.push(newVersion);
					}
				}
			}
		}
	}

	return 0;
}

