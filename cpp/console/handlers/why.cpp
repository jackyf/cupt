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
using std::priority_queue;
#include <stack>
using std::stack;

using std::cout;
using std::endl;

struct PathEntry
{
	size_t length = 0;
	const BinaryVersion* version = nullptr;
	BinaryVersion::RelationTypes::Type dependencyType;
	const RelationExpression* relationExpressionPtr;
};

struct Edge
{
	const BinaryVersion* version;
	PathEntry pathEntry;

	bool operator<(const Edge& other) const
	{
		return pathEntry.length > other.pathEntry.length;
	}
};

inline size_t getDependencyTypePenalty(BinaryVersion::RelationTypes::Type dp)
{
	switch (dp)
	{
		case BinaryVersion::RelationTypes::Recommends:
			return 1;
		case BinaryVersion::RelationTypes::Suggests:
			return 3;
		default:
			return 0;
	}
}

struct VersionsAndLinks
{
	priority_queue<Edge> versions;
	map<const BinaryVersion*, PathEntry> links;

	void addStartingVersion(const BinaryVersion* version)
	{
		versions.push({ version, PathEntry() });
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

	bool setEdge(const Edge& edge)
	{
		auto insertResult = links.insert({ edge.version, edge.pathEntry });
		return insertResult.second;
	}

	void addVersionRelationExpressions(const Cache& cache,
			Edge edge, BinaryVersion::RelationTypes::Type dependencyType)
	{
		auto version = edge.version;
		auto newLength = edge.pathEntry.length + getDependencyTypePenalty(dependencyType);

		for (const auto& relationExpression: version->relations[dependencyType])
		{
			// insert recursive depends into queue
			for (const auto& newVersion: cache.getSatisfyingVersions(relationExpression))
			{
				PathEntry newPathEntry;
				newPathEntry.length = newLength;
				newPathEntry.version = version;
				newPathEntry.dependencyType = dependencyType;
				newPathEntry.relationExpressionPtr = &relationExpression;

				versions.push({ newVersion, newPathEntry });
			}
		}
	}
};

void printPath(const VersionsAndLinks& val, const BinaryVersion* version)
{
	stack<PathEntry> path;
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
		const auto& pathEntry = path.top();
		path.pop();
		cout << format2("%s %s: %s: %s",
				pathEntry.version->packageName, pathEntry.version->versionString,
				__(BinaryVersion::RelationTypes::strings[pathEntry.dependencyType].c_str()),
				pathEntry.relationExpressionPtr->toString()) << endl;
	}
}

vector<BinaryVersion::RelationTypes::Type> getSignificantRelationGroups(const Config& config)
{
	vector<BinaryVersion::RelationTypes::Type> result;

	result.push_back(BinaryVersion::RelationTypes::PreDepends);
	result.push_back(BinaryVersion::RelationTypes::Depends);
	if (config.getBool("cupt::resolver::keep-recommends"))
	{
		result.push_back(BinaryVersion::RelationTypes::Recommends);
	}
	if (config.getBool("cupt::resolver::keep-suggests"))
	{
		result.push_back(BinaryVersion::RelationTypes::Suggests);
	}

	return result;
}

std::tuple<shared_ptr<const Cache>, vector<string>> parseArguments(Context& context)
{
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

	return make_tuple(std::move(cache), std::move(arguments));
}

const BinaryVersion* extractLeafVersion(const Cache& cache, vector<string>* arguments)
{
	auto leafPackageExpression = arguments->back();
	arguments->erase(arguments->end() - 1);
	return selectBinaryVersionsWildcarded(cache, leafPackageExpression, true)[0];
}

int findDependencyChain(Context& context)
{
	// turn off info parsing, we don't need it, only relations
	if(!shellMode)
	{
		Version::parseInfoOnly = false;
	}

	vector<string> arguments;
	shared_ptr<const Cache> cache;
	std::tie(cache, arguments) = parseArguments(context);

	auto leafVersion = extractLeafVersion(*cache, &arguments);

	VersionsAndLinks val;
	val.initialise(*cache, arguments);

	auto config = context.getConfig();

	auto relationGroups = getSignificantRelationGroups(*config);

	while (!val.versions.empty())
	{
		auto edge = val.versions.top();
		val.versions.pop();

		bool isNewEdge = val.setEdge(edge);

		if (edge.version == leafVersion)
		{
			printPath(val, edge.version); // we found a path, re-walk it
			break;
		}

		if (isNewEdge)
		{
			for (auto dependencyType: relationGroups)
			{
				val.addVersionRelationExpressions(*cache, edge, dependencyType);
			}
		}
	}

	return 0;
}

