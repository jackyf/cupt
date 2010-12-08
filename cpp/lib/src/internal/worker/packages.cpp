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
#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/releaseinfo.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/file.hpp>
#include <cupt/download/progress.hpp>

#include <internal/filesystem.hpp>
#include <internal/debdeltahelper.hpp>
#include <internal/lock.hpp>

#include <internal/worker/packages.hpp>

namespace cupt {
namespace internal {

PackagesWorker::PackagesWorker()
{
	__auto_installed_package_names = _cache->getExtendedInfo().automaticallyInstalled;
}

void PackagesWorker::__fill_actions(GraphAndAttributes& gaa)
{
	typedef InnerAction IA;

	// user action - action name from actions preview
	const map< Action::Type, vector< IA::Type > > actionsMapping = {
		{ Action::Install, vector< IA::Type >{ IA::Unpack, IA::Configure } },
		{ Action::Upgrade, vector< IA::Type >{ IA::Remove, IA::Unpack, IA::Configure } },
		{ Action::Downgrade, vector< IA::Type >{ IA::Remove, IA::Unpack, IA::Configure } },
		{ Action::Configure, vector< IA::Type >{ IA::Configure } },
		{ Action::Deconfigure, vector< IA::Type >{ IA::Remove } },
		{ Action::Remove, vector< IA::Type >{ IA::Remove } },
		{ Action::Purge, vector< IA::Type >{ IA::Remove } },
	};

	// packages with indirect upgrades have explicit 'remove' action, so
	// their 'remove' subactions are not fake
	set< string > fakeExceptions;
	{
		auto list = _config->getList("cupt::worker::allow-indirect-upgrade");
		FORIT(it, list)
		{
			fakeExceptions.insert(*it);
		}
	}

	// convert all actions into inner ones
	FORIT(mapIt, actionsMapping)
	{
		const Action::Type& userAction = mapIt->first;
		const vector< IA::Type >& innerActionTypes = mapIt->second;

		FORIT(suggestedPackageIt, (*__actions_preview)[userAction])
		{
			const string& packageName = suggestedPackageIt->first;

			const InnerAction* previousInnerActionPtr = NULL;

			FORIT(innerActionTypeIt, innerActionTypes)
			{
				const IA::Type& innerActionType = *innerActionTypeIt;

				shared_ptr< const BinaryVersion > version;
				if (innerActionType == IA::Remove)
				{
					auto package = _cache->getBinaryPackage(packageName);
					if (package)
					{
						version = package->getInstalledVersion(); // may be undef too in purge-only case
					}
				}
				else
				{
					version = suggestedPackageIt->second.version;
				}
				if (!version)
				{
					auto versionPtr = new BinaryVersion;
					versionPtr->packageName = packageName;
					versionPtr->versionString = __dummy_version_string;
					version.reset(versionPtr);
				}

				InnerAction action;
			    action.version = version;
				action.type = innerActionType;
				action.fake = false;
				if (innerActionType == IA::Remove && innerActionTypes.size() > 1)
				{
					if (!fakeExceptions.count(packageName))
					{
						// this is fake 'remove' action which will be merged
						// with 'unpack' unconditionally later
						action.fake = true;
					}
				}

				auto newVertexPtr = gaa.graph.addVertex(std::move(action));

				if (previousInnerActionPtr)
				{
					// the edge between consecutive actions
					gaa.graph.addEdge(*previousInnerActionPtr, action);
				}
				previousInnerActionPtr = newVertexPtr;
			}
		}
	}
}

void PackagesWorker::__fill_action_dependencies(
		const shared_ptr< const BinaryVersion >& version,
		BinaryVersion::RelationTypes::Type dependencyType, InnerAction::Type actionType,
		Direction::Type direction, const InnerAction& innerAction,
		GraphAndAttributes& gaa, bool debugging)
{
	const set< InnerAction >& verticesMap = gaa.graph.getVertices();

	InnerAction candidateAction;
	candidateAction.type = actionType;

	const RelationLine& relationLine = version->relations[dependencyType];
	FORIT(relationExpressionIt, relationLine)
	{
		auto satisfyingVersions = _cache->getSatisfyingVersions(*relationExpressionIt);

		FORIT(satisfyingVersionIt, satisfyingVersions)
		{
			candidateAction.version = *satisfyingVersionIt;

			// search for the appropriate action in action list
			auto vertexIt = verticesMap.find(candidateAction);
			if (vertexIt == verticesMap.end())
			{
				continue;
			}
			const InnerAction& currentAction = *vertexIt;
			if (innerAction.fake && currentAction.fake)
			{
				continue;
			}

			const InnerAction& masterAction = (direction == Direction::After ? currentAction : innerAction);
			const InnerAction& slaveAction = (direction == Direction::After ? innerAction : currentAction);

			// TODO: uncomment or delete, depending on #582423
			/* bool replacesFound = false;
			if (dependencyType == BinaryVersion::RelationTypes::Conflicts)
			{
				// this is Conflicts, in the case there are appropriate
				// Replaces, the 'remove before' action dependency should not be created
				const RelationLine& replaces = masterAction.version->relations[BinaryVersion::RelationTypes::Replaces];
				FORIT(replacesRelationExpressionIt, replaces)
				{
					auto replacesSatisfyingVersions = cache->getSatisfyingVersions(*replacesRelationExpressionIt);

					auto predicate = std::bind2nd(PointerEqual< const BinaryVersion >(), slaveAction.version);
					if (std::find_if(replacesSatisfyingVersions.begin(), replacesSatisfyingVersions.end(),
							predicate) != replacesSatisfyingVersions.end())
					{
						// yes, found Replaces, skip this action
						replacesFound = true;
						break;
					}
				}
			}
			if (replacesFound)
			{
				continue;
			}
			*/

			gaa.graph.addEdge(slaveAction, masterAction);

			// adding relation to attributes
			vector< GraphAndAttributes::RelationInfoRecord >& relationInfo =
					gaa.attributes[slaveAction][masterAction].relationInfo;
			GraphAndAttributes::RelationInfoRecord record =
					{ dependencyType, *relationExpressionIt, direction == Direction::After };
			relationInfo.push_back(std::move(record));

			if (debugging)
			{
				debug("new action dependency: '%s' -> '%s', reason: '%s: %s'", slaveAction.toString().c_str(),
						masterAction.toString().c_str(), BinaryVersion::RelationTypes::rawStrings[dependencyType],
						relationExpressionIt->toString().c_str());
			}
		}
	}
}

void PackagesWorker::__fill_graph_dependencies(GraphAndAttributes& gaa)
{
	typedef BinaryVersion::RelationTypes RT;

	bool debugging = _config->getBool("debug::worker");

	// fill the actions' dependencies
	const set< InnerAction >& vertices = gaa.graph.getVertices();
	FORIT(vertexIt, vertices)
	{
		const InnerAction& innerAction = *vertexIt;
		const shared_ptr< const BinaryVersion >& version = innerAction.version;
		switch (innerAction.type)
		{
			case InnerAction::Unpack:
			{
				process_unpack:

				// pre-depends must be unpacked before
				__fill_action_dependencies(version, RT::PreDepends, InnerAction::Configure,
						Direction::Before, innerAction, gaa, debugging);
				// conflicts must be unsatisfied before
				__fill_action_dependencies(version, RT::Conflicts, InnerAction::Remove,
						Direction::Before, innerAction, gaa, debugging);
				// breaks must be unsatisfied before (yes, before the unpack)
				__fill_action_dependencies(version, RT::Breaks, InnerAction::Remove,
						Direction::Before, innerAction, gaa, debugging);
			}
				break;
			case InnerAction::Configure:
			{
				// depends must be configured before
				__fill_action_dependencies(version, RT::Depends, InnerAction::Configure,
						Direction::Before, innerAction, gaa, debugging);

				// it has also to be unpacked if the same version was not in state 'unpacked'
				// search for the appropriate unpack action
				auto candidateAction = innerAction;
				candidateAction.type = InnerAction::Unpack;

				if (!vertices.count(candidateAction))
				{
					// add unpack-level prerequisities to this configure action
					goto process_unpack;
				}
			}
				break;
			case InnerAction::Remove:
			{
				// pre-depends must be removed after
				__fill_action_dependencies(version, RT::PreDepends, InnerAction::Remove,
						Direction::After, innerAction, gaa, debugging);
				// depends must be removed after
				__fill_action_dependencies(version, RT::Depends, InnerAction::Remove,
						Direction::After, innerAction, gaa, debugging);
				// conflicts may be satisfied only after
				__fill_action_dependencies(version, RT::Conflicts, InnerAction::Unpack,
						Direction::After, innerAction, gaa, debugging);
				// breaks may be satisfied only after
				__fill_action_dependencies(version, RT::Breaks, InnerAction::Unpack,
						Direction::After, innerAction, gaa, debugging);
				// in the previous case it may happen that package was already unpacked
				// with breaking dependencies already, so there won't be 'unpack' action but just
				// 'configure' one, so set dependency to 'configure' too just in case
				__fill_action_dependencies(version, RT::Breaks, InnerAction::Configure,
						Direction::After, innerAction, gaa, debugging);
			}
		}
	}
}

void PackagesWorker::__check_graph_pre_depends(GraphAndAttributes& gaa, bool debugging)
{
	auto edges = gaa.graph.getEdges();
	FORIT(edgeIt, edges)
	{
		const InnerAction& from = *(edgeIt->first);
		const InnerAction& to = *(edgeIt->second);
		const vector< GraphAndAttributes::RelationInfoRecord >& records =
				gaa.attributes[from][to].relationInfo;

		RelationLine preDependencyRelationExpressions;
		FORIT(recordIt, records)
		{
			if (recordIt->dependencyType == BinaryVersion::RelationTypes::PreDepends)
			{
				preDependencyRelationExpressions.push_back(recordIt->relationExpression);
			}
		}
		if (preDependencyRelationExpressions.empty())
		{
			continue;
		}

		if (debugging)
		{
			debug("checking edge '%s' -> '%s' for pre-dependency cycle",
					from.toString().c_str(), to.toString().c_str());
		}
		if (gaa.graph.getReachable(to).count(from))
		{
			// bah! the pre-dependency cannot be overridden
			// it is not worker's fail (at least, it shouldn't be)

			auto path = gaa.graph.getPathVertices(to, from);

			vector< string > packageNamesInPath;
			FORIT(pathIt, path)
			{
				packageNamesInPath.push_back((*pathIt)->version->packageName);
			}
			string packageNamesString = join(", ", packageNamesInPath);

			warn("the pre-dependency(ies) '%s' will be broken during the actions, the packages involved: %s",
					preDependencyRelationExpressions.toString().c_str(), packageNamesString.c_str());
		}
	}
}

vector< pair< InnerAction, InnerAction > > __emplace_virtual_edges(
		GraphAndAttributes& gaa, const shared_ptr< const Cache >& cache)
{
	/* here we also adding adding fake-antiupgrades for all packages in the system
	   which stay unmodified for the sake of getting inter-dependencies between
	   the optional dependecies like

	   'abc' depends 'x | y', abc stays unmodified, x goes away, y is going to be installed

	   here, action 'remove x' are dependent on 'install y' one and it gets
	   introduced by

	   'install y' -> 'install abc' <-> 'remove abc' -> 'remove x'
	                  {----------------------------}
	                              <merge>
	   which goes into

	   'install y' -> 'remove x'

	   moreover, we split the installed version into virtual version by one
	   relation expression, so different relation expressions of the same real
	   version don't interact with each other, otherwise we'd get a full cyclic
	   mess
	*/
	vector< pair< InnerAction, InnerAction > > virtualEdges;

	set< string > blacklistedPackageNames;
	// building the black list
	FORIT(vertexIt, gaa.graph.getVertices())
	{
		blacklistedPackageNames.insert(vertexIt->version->packageName);
	}

	auto installedVersions = cache->getInstalledVersions();
	FORIT(installedVersionIt, installedVersions)
	{
		const shared_ptr< const BinaryVersion > installedVersion = *installedVersionIt;
		const string& packageName = installedVersion->packageName;
		if (blacklistedPackageNames.count(packageName))
		{
			continue;
		}

		typedef BinaryVersion::RelationTypes RT;
		static const vector< RT::Type > dependencyTypes = { RT::PreDepends, RT::Depends };
		FORIT(dependencyTypeIt, dependencyTypes)
		{
			FORIT(relationExpressionIt, installedVersion->relations[*dependencyTypeIt])
			{
				shared_ptr< BinaryVersion > virtualVersion(new BinaryVersion);
				virtualVersion->packageName = packageName + " [" + relationExpressionIt->toString() + "]";
				virtualVersion->versionString = installedVersion->versionString;
				virtualVersion->relations[*dependencyTypeIt].push_back(*relationExpressionIt);

				InnerAction from;
				from.version = virtualVersion;
				from.type = InnerAction::Configure;
				from.fake = true;

				InnerAction to;
				to.version = virtualVersion;
				to.type = InnerAction::Remove;
				to.fake = true;

				// we don't add edge here, but add the vertexes to gain dependencies and
				// save the vertexes order
				gaa.graph.addVertex(from);
				gaa.graph.addVertex(to);
				virtualEdges.push_back(std::make_pair(from, to));
			}
		}
	}

	return virtualEdges;
}

void __unite_needed(const shared_ptr< const Config >& config, GraphAndAttributes& gaa,
		const vector< pair< InnerAction, InnerAction > >& virtualEdges)
{
	vector< pair< const InnerAction*, const InnerAction* > > changes;

	// pre-fill the list of downgrades/upgrades vertices
	const set< InnerAction >& vertices = gaa.graph.getVertices();
	FORIT(innerActionIt, vertices)
	{
		if (innerActionIt->type == InnerAction::Remove)
		{
			InnerAction searchAction = *innerActionIt;

			const list< const InnerAction* >& successors = gaa.graph.getSuccessorsFromPointer(&*innerActionIt);
			FORIT(actionPtrIt, successors)
			{
				if ((*actionPtrIt)->type == InnerAction::Unpack &&
					(*actionPtrIt)->version->packageName == innerActionIt->version->packageName)
				{
					changes.push_back(std::make_pair(&*innerActionIt, *actionPtrIt));
				}
			}
		}
	}

	bool debugging = config->getBool("debug::worker");

	auto moveEdge = [&gaa, debugging](const InnerAction& fromPredecessor, const InnerAction& fromSuccessor,
			const InnerAction& toPredecessor, const InnerAction& toSuccessor)
	{
		if (debugging)
		{
			debug("moving edge '%s' -> '%s' to edge '%s' -> '%s'",
					fromPredecessor.toString().c_str(), fromSuccessor.toString().c_str(),
					toPredecessor.toString().c_str(), toSuccessor.toString().c_str());
		}
		if (toPredecessor == toSuccessor)
		{
			return;
		}
		GraphAndAttributes::Attribute& toAttribute = gaa.attributes[toPredecessor][toSuccessor];
		GraphAndAttributes::Attribute& fromAttribute = gaa.attributes[fromPredecessor][fromSuccessor];

		// concatenating relationInfo
		toAttribute.relationInfo.insert(toAttribute.relationInfo.end(),
				fromAttribute.relationInfo.begin(), fromAttribute.relationInfo.end());

		if (fromAttribute.multiplied)
		{
			toAttribute.multiplied = true;
		}

		// delete the whole attribute
		gaa.attributes[fromPredecessor].erase(fromSuccessor);

		gaa.graph.deleteEdge(fromPredecessor, fromSuccessor);
		gaa.graph.addEdge(toPredecessor, toSuccessor);
	};

	{ // get rid of virtual edges
		FORIT(edgeIt, virtualEdges)
		{
			const InnerAction& from = edgeIt->first;
			const InnerAction& to = edgeIt->second;

			// "multiplying" the dependencies
			const list< const InnerAction* > predecessors = gaa.graph.getPredecessors(from);
			const list< const InnerAction* > successors = gaa.graph.getSuccessors(to);
			FORIT(predecessorVertexPtrIt, predecessors)
			{
				FORIT(successorVertexPtrIt, successors)
				{
					// moving edge attributes too
					moveEdge(**predecessorVertexPtrIt, from, **predecessorVertexPtrIt, **successorVertexPtrIt);
					moveEdge(to, **successorVertexPtrIt, **predecessorVertexPtrIt, **successorVertexPtrIt);
					gaa.attributes[**predecessorVertexPtrIt][**successorVertexPtrIt].multiplied = true;
					if (debugging)
					{
						const string& mediatorPackageName = from.version->packageName;
						debug("multiplied action dependency: '%s' -> '%s', virtual mediator: '%s'",
								(*predecessorVertexPtrIt)->toString().c_str(), (*successorVertexPtrIt)->toString().c_str(),
								mediatorPackageName.c_str());
					}

				}
			}

			gaa.graph.deleteVertex(from);
			gaa.graph.deleteVertex(to);
		}
	}

	{ // unit remove and unpack
		auto mergeExceptionPackageNames = config->getList("cupt::worker::allow-indirect-upgrade");
		FORIT(changeIt, changes)
		{
			const InnerAction& from = *(changeIt->first);
			const InnerAction& to = *(changeIt->second);

			const string& packageName = from.version->packageName;
			if (std::find(mergeExceptionPackageNames.begin(), mergeExceptionPackageNames.end(),
						packageName) == mergeExceptionPackageNames.end())
			{
				const list< const InnerAction* > successors = gaa.graph.getSuccessorsFromPointer(&from);
				const list< const InnerAction* > predecessors = gaa.graph.getPredecessorsFromPointer(&from);
				FORIT(successorPtrIt, successors)
				{
					moveEdge(from, **successorPtrIt, to, **successorPtrIt);
				}
				FORIT(predecessorPtrIt, predecessors)
				{
					moveEdge(**predecessorPtrIt, from, **predecessorPtrIt, to);
				}
				gaa.graph.deleteVertex(from);
			}
		}
	}
}

bool PackagesWorker::__build_actions_graph(GraphAndAttributes& gaa)
{
	if (!__desired_state)
	{
		fatal("worker desired state is not given");
	}

	__fill_actions(gaa);

	// maybe, we have nothing to do?
	if (gaa.graph.getVertices().empty())
	{
		return false;
	}

	auto virtualEdges = __emplace_virtual_edges(gaa, _cache);

	__fill_graph_dependencies(gaa);
	__unite_needed(_config, gaa, virtualEdges);

	bool debugging = _config->getBool("debug::worker");
	if (debugging)
	{
		auto edges = gaa.graph.getEdges();
		FORIT(edgeIt, edges)
		{
			debug("the present action dependency: '%s' -> '%s'",
					edgeIt->first->toString().c_str(), edgeIt->second->toString().c_str());
		}
	}

	__check_graph_pre_depends(gaa, debugging);

	return true;
}

void __split_heterogeneous_actions(
		vector< vector< InnerAction > >& actionGroups, GraphAndAttributes& gaa)
{
	vector< vector< InnerAction > > newActionGroups;

	FORIT(actionGroupIt, actionGroups)
	{
		// all the actions in the group will have the same type by algorithm
		const vector< InnerAction >& actionGroup = *actionGroupIt;
		if (actionGroup.size() > 1)
		{
			// multiple actions at once

			// firstly, we need to cope with conflicts at early stage, so we build a mini-graph
			internal::Graph< InnerAction > miniActionGraph;

			vector< pair< const InnerAction*, const InnerAction* > > edges;
			// pre-fill edges and fill vertices
			FORIT(actionIt, actionGroup)
			{
				const list< const InnerAction* >& predecessors = gaa.graph.getPredecessors(*actionIt);
				const list< const InnerAction* >& successors = gaa.graph.getSuccessors(*actionIt);
				FORIT(predecessorPtrIt, predecessors)
				{
					edges.push_back(std::make_pair(*predecessorPtrIt, &*actionIt));
				}
				FORIT(successorPtrIt, successors)
				{
					edges.push_back(std::make_pair(&*actionIt, *successorPtrIt));
				}
				miniActionGraph.addVertex(*actionIt);
			}
			// filter edges
			const set< InnerAction >& allowedVertices = miniActionGraph.getVertices();
			FORIT(edgeIt, edges)
			{
				const InnerAction& from = *(edgeIt->first);
				const InnerAction& to = *(edgeIt->second);
				if (!allowedVertices.count(from) || !allowedVertices.count(to))
				{
					continue; // edge lies outside our mini graph
				}

				const GraphAndAttributes::Attribute& attribute = gaa.attributes[from][to];
				if (attribute.multiplied)
				{
					continue;
				}

				bool suitable = false;
				if (attribute.relationInfo.empty())
				{
					suitable = true;
				}
				else
				{
					FORIT(recordIt, attribute.relationInfo)
					{
						if (recordIt->dependencyType != BinaryVersion::RelationTypes::Breaks &&
							(!recordIt->reverse || recordIt->dependencyType == BinaryVersion::RelationTypes::Conflicts))
						{
							suitable = true;
							break;
						}
					}
				}
				if (suitable)
				{
					miniActionGraph.addEdge(from, to);
				}
			}

			auto actionSubgroupsSorted =
					miniActionGraph.topologicalSortOfStronglyConnectedComponents();

			FORIT(actionSubgroupIt, actionSubgroupsSorted)
			{
				vector< InnerAction >& actionSubgroup = *actionSubgroupIt;
				if (actionSubgroup.size() > 1)
				{
					// only circular configures allowed
					FORIT(actionIt, actionSubgroup)
					{
						if (actionIt->type != InnerAction::Configure)
						{
							// ooh, mixed circular dependency? no-go
							vector< string > actionStrings;
							FORIT(it, actionSubgroup)
							{
								actionStrings.push_back(it->toString());
							}
							auto suggestionString = __("you may try to work around by adding one or more affected packages "
									"to the 'cupt::worker::allow-indirect-upgrade' configuration variable");
							fatal("unable to schedule circular actions '%s', %s",
									join(", ", actionStrings).c_str(), suggestionString.c_str());
						}
					}
				}

				// always set forcing all dependencies
				// dpkg requires to pass both --force-depends and --force-breaks to achieve it
				actionSubgroup[0].dpkgFlags = " --force-depends --force-breaks";

				newActionGroups.push_back(actionSubgroup);
			}
		}
		else
		{
			newActionGroups.push_back(actionGroup);
		}
	}

	newActionGroups.swap(actionGroups);
}

void __move_unpacks_to_configures (vector< vector< InnerAction > >& actionGroups,
		const GraphAndAttributes& gaa)
{
	auto move = [&gaa, &actionGroups](const InnerAction::Type actionType)
	{
		FORIT(actionGroupIt, actionGroups)
		{
			bool suitable = true;
			FORIT(actionIt, *actionGroupIt)
			{
				if (actionIt->type != actionType)
				{
					// some other action, including heterogeneous variant
					suitable = false;
					break;
				}
			}
			if (!suitable)
			{
				continue;
			}

			list< const list< const InnerAction* >* > disallowedPairs;
			FORIT(actionIt, *actionGroupIt)
			{
				const list< const InnerAction* >& pairs = (actionType == InnerAction::Unpack) ?
						gaa.graph.getSuccessors(*actionIt) : gaa.graph.getPredecessors(*actionIt);
				disallowedPairs.push_back(&pairs);
			}
			const vector< InnerAction > actionGroup = *actionGroupIt;

			// ok, try to move it as right as possible
			auto nextIt = actionGroupIt + 1;
			while (nextIt != actionGroups.end())
			{
				vector< InnerAction >& next = *nextIt;

				FORIT(nextActionIt, next)
				{
					FORIT(listPtrIt, disallowedPairs)
					{
						FORIT(pairPtrIt, **listPtrIt)
						{
							if (*nextActionIt == **pairPtrIt)
							{
								// cannot move
								goto last_try;
							}
						}
					}
				}

				// move!
				vector< InnerAction >& current = *(nextIt - 1);
				current.swap(next);

				++nextIt;
			}

			last_try:
			;
		}
	};

	move(InnerAction::Unpack);
	std::reverse(actionGroups.begin(), actionGroups.end());
	move(InnerAction::Configure);
	std::reverse(actionGroups.begin(), actionGroups.end());
}

map< string, pair< download::Manager::DownloadEntity, string > > PackagesWorker::__prepare_downloads()
{
	auto archivesDirectory = _get_archives_directory();

	if (!_config->getBool("cupt::worker::simulate"))
	{
		string partialDirectory = archivesDirectory + partialDirectorySuffix;
		if (! fs::dirExists(partialDirectory))
		{
			if (mkdir(partialDirectory.c_str(), 0755) == -1)
			{
				fatal("unable to create partial directory '%s': EEE", partialDirectory.c_str());
			}
		}
	}

	map< string, pair< download::Manager::DownloadEntity, string > > downloads;

	DebdeltaHelper debdeltaHelper;

	static const vector< Action::Type > actions = { Action::Install, Action::Upgrade, Action::Downgrade };
	FORIT(actionIt, actions)
	{
		const Resolver::SuggestedPackages& suggestedPackages = (*__actions_preview)[*actionIt];
		FORIT(it, suggestedPackages)
		{
			const shared_ptr< const BinaryVersion >& version = it->second.version;

			const string& packageName = version->packageName;
			const string& versionString = version->versionString;

			auto downloadInfo = version->getDownloadInfo();

			// we need at least one real URI
			if (downloadInfo.empty())
			{
				fatal("no available download URIs for %s %s", packageName.c_str(), versionString.c_str());
			}

			// paths
			auto basename = _get_archive_basename(version);
			auto downloadPath = archivesDirectory + partialDirectorySuffix + '/' + basename;
			auto targetPath = archivesDirectory + '/' + basename;

			// exclude from downloading packages that are already present
			if (fs::fileExists(targetPath) &&
				version->file.hashSums.verify(targetPath))
			{
				continue;
			}

			download::Manager::DownloadEntity downloadEntity;

			const shared_ptr< const ReleaseInfo > release = version->sources[0].release;
			string longAliasTail = sf("%s/%s %s %s", release->codename.c_str(),
						release->component.c_str(), packageName.c_str(), versionString.c_str());
			FORIT(it, downloadInfo)
			{
				string uri = it->baseUri + '/' + it->directory + '/' + version->file.name;


				string shortAlias = packageName;
				string longAlias = it->baseUri + ' ' + longAliasTail;

				downloadEntity.extendedUris.push_back(
						download::Manager::ExtendedUri(uri, shortAlias, longAlias));
			}
			{
				auto debdeltaDownloadInfo = debdeltaHelper.getDownloadInfo(version, _cache);
				FORIT(it, debdeltaDownloadInfo)
				{
					const string& uri = it->uri;
					string longAlias = it->baseUri + ' ' + longAliasTail;

					downloadEntity.extendedUris.push_back(
							download::Manager::ExtendedUri(uri, packageName, longAlias));
				}
			}

			// caution: targetPath and downloadEntity.targetPath are different
			downloadEntity.targetPath = downloadPath;
			downloadEntity.size = version->file.size;

			downloadEntity.postAction = [version, downloadPath, targetPath]() -> string
			{
				if (!fs::fileExists(downloadPath))
				{
					return __("unable to find downloaded file");
				}
				if (!version->file.hashSums.verify(downloadPath))
				{
					unlink(downloadPath.c_str()); // intentionally ignore errors if any
					return __("hash sums mismatch");
				}

				return fs::move(downloadPath, targetPath);
			};

			auto downloadValue = std::make_pair(std::move(downloadEntity), targetPath);
			downloads.insert(std::make_pair(packageName, downloadValue));
		}
	}

	return downloads;
}

vector< Changeset > __split_action_groups_into_changesets(
		const vector< vector< InnerAction > >& actionGroups,
		const map< string, pair< download::Manager::DownloadEntity, string > >& downloads)
{
	vector< Changeset > result;
	Changeset changeset;
	set< string > unpackedPackageNames;

	FORIT(actionGroupIt, actionGroups)
	{
		auto actionType = (*actionGroupIt)[0].type;

		FORIT(actionIt, *actionGroupIt)
		{
			const string& packageName = actionIt->version->packageName;
			if (actionType == InnerAction::Unpack)
			{
				unpackedPackageNames.insert(packageName);

				auto downloadEntryIt = downloads.find(packageName);
				if (downloadEntryIt != downloads.end())
				{
					// don't need package name anymore
					changeset.downloads.push_back(downloadEntryIt->second);
				}
			}
			else if (actionType == InnerAction::Configure)
			{
				unpackedPackageNames.erase(packageName);
			}
		}

		changeset.actionGroups.push_back(*actionGroupIt);

		if (unpackedPackageNames.empty())
		{
			// all unpacked packages are configured, the end of changeset
			result.push_back(std::move(changeset));

			changeset.actionGroups.clear();
			changeset.downloads.clear();
		}
	}

	if (!unpackedPackageNames.empty())
	{
		vector< string > unconfiguredPackageNames;
		std::copy(unpackedPackageNames.begin(), unpackedPackageNames.end(),
				std::back_inserter(unconfiguredPackageNames));
		fatal("internal error: packages stay unconfigured: '%s'",
				join(" ", unconfiguredPackageNames).c_str());
	}

	return result;
}

size_t __get_download_amount(const Changeset& changeset)
{
	const vector< pair< download::Manager::DownloadEntity, string > >& source = changeset.downloads;

	size_t amount = 0;
	FORIT(it, source)
	{
		amount += it->first.size;
	}

	return amount;
}

size_t __get_max_download_amount(const vector< Changeset >& changesets, bool debugging)
{
	size_t result = 0;

	vector< string > amounts; // for debugging
	FORIT(changesetIt, changesets)
	{
		auto amount = __get_download_amount(*changesetIt);
		if (amount > result)
		{
			result = amount;
		}
		if (debugging)
		{
			amounts.push_back(humanReadableSizeString(amount));
		}
	}
	if (debugging)
	{
		debug("the changeset download amounts: maximum: %s, all: %s",
				humanReadableSizeString(result).c_str(), join(", ", amounts).c_str());
	}

	return result;
}

vector< Changeset > PackagesWorker::__get_changesets(GraphAndAttributes& gaa,
		const map< string, pair< download::Manager::DownloadEntity, string > >& downloads)
{
	auto debugging = _config->getBool("debug::worker");
	size_t archivesSpaceLimit = _config->getInteger("cupt::worker::archives-space-limit");
	size_t archivesSpaceLimitTries = _config->getInteger("cupt::worker::archives-space-limit::tries");

	bool ok = false;
	size_t minMaxDownloadAmount = -1;

	vector< Changeset > changesets;

	for (size_t i = 0; i < archivesSpaceLimitTries; ++i)
	{
		auto actionGroups = gaa.graph.topologicalSortOfStronglyConnectedComponents();
		__move_unpacks_to_configures(actionGroups, gaa);
		__split_heterogeneous_actions(actionGroups, gaa);

		changesets = __split_action_groups_into_changesets(actionGroups, downloads);

		if (!archivesSpaceLimit)
		{
			ok = true;
			break;
		}

		auto maxDownloadAmount = __get_max_download_amount(changesets, debugging);
		if (minMaxDownloadAmount > maxDownloadAmount)
		{
			minMaxDownloadAmount = maxDownloadAmount;
		}
		if (debugging)
		{
			debug("the changeset download amounts: minimum maximum: %s",
					humanReadableSizeString(minMaxDownloadAmount).c_str());
		}
		if (maxDownloadAmount <= archivesSpaceLimit)
		{
			ok = true;
			break;
		}
	}

	if (ok)
	{
		auto doesDownloadAmountFit = [&archivesSpaceLimit](const size_t amount) -> bool
		{
			return !archivesSpaceLimit || amount <= archivesSpaceLimit;
		};

		vector< Changeset > newChangesets = { Changeset() }; // with one empty element
		FORIT(changesetIt, changesets)
		{
			const Changeset& nextChangeset = *changesetIt;
			Changeset& currentChangeset = *(newChangesets.rbegin());

			auto currentDownloadAmount = __get_download_amount(currentChangeset);
			auto nextDownloadAmount = __get_download_amount(nextChangeset);
			if (doesDownloadAmountFit(currentDownloadAmount + nextDownloadAmount))
			{
				// merge!
				currentChangeset.actionGroups.insert(currentChangeset.actionGroups.end(),
						nextChangeset.actionGroups.begin(), nextChangeset.actionGroups.end());
				currentChangeset.downloads.insert(currentChangeset.downloads.end(),
						nextChangeset.downloads.begin(), nextChangeset.downloads.end());
			}
			else
			{
				newChangesets.push_back(nextChangeset);
			}
		}
		changesets.swap(newChangesets);
	}
	else
	{
		// we failed to fit in limit
		fatal("unable to fit in archives space limit '%zu', best try is '%zu'",
				archivesSpaceLimit, minMaxDownloadAmount);
	}

	return changesets;
}

void PackagesWorker::__run_dpkg_command(const string& flavor, const string& command, const string& alias)
{
	auto errorId = sf(__("dpkg '%s' action '%s'"), flavor.c_str(), alias.c_str());
	_run_external_command(command, errorId);
}

void PackagesWorker::__clean_downloads(const Changeset& changeset)
{
	internal::Lock archivesLock(_config, _get_archives_directory() + "/lock");

	bool simulating = _config->getBool("cupt::worker::simulate");
	FORIT(it, changeset.downloads)
	{
		const char* targetPath = it->second.c_str();
		if (simulating)
		{
			simulate("removing archive '%s'", targetPath);
		}
		else
		{
			if (unlink(targetPath) == -1)
			{
				fatal("unable to remove file '%s': EEE", targetPath);
			}
		}
	}
}

void PackagesWorker::__do_dpkg_pre_actions()
{
	auto commands = _config->getList("dpkg::pre-invoke");
	FORIT(commandIt, commands)
	{
		__run_dpkg_command("post", *commandIt, *commandIt);
	}
}

string PackagesWorker::__generate_input_for_preinstall_v2_hooks(
		const vector< vector< InnerAction > >& actionGroups)
{
	// all hate undocumented formats...
	string result = "VERSION 2\n";

	{ // writing out a configuration
		auto printKeyValue = [&result](const string& key, const string& value)
		{
			if (!value.empty())
			{
				result += (key + "=" + value + "\n");
			}
		};

		{
			auto regularKeys = _config->getScalarOptionNames();
			FORIT(keyIt, regularKeys)
			{
				printKeyValue(*keyIt, _config->getString(*keyIt));
			}
		}
		{
			auto listKeys = _config->getListOptionNames();
			FORIT(keyIt, listKeys)
			{
				const string& key = *keyIt;
				auto values = _config->getList(key);
				FORIT(valueIt, values)
				{
					printKeyValue(key + "::", *valueIt);
				}
			}
		}

		result += "\n";
	}

	auto archivesDirectory = _get_archives_directory();
	FORIT(actionGroupIt, actionGroups)
	{
		auto actionType = actionGroupIt->begin()->type;

		FORIT(actionIt, *actionGroupIt)
		{
			const shared_ptr< const BinaryVersion >& version = actionIt->version;
			const string& packageName = version->packageName;

			string oldVersionString = "-";
			auto oldPackage = _cache->getBinaryPackage(packageName);
			if (oldPackage)
			{
				auto installedVersion = oldPackage->getInstalledVersion();
				if (installedVersion)
				{
					oldVersionString = installedVersion->versionString;
				}
			}
			string newVersionString = (actionType == InnerAction::Remove ? "-" : version->versionString);

			string compareVersionStringsSign;
			if (oldVersionString == "-")
			{
				compareVersionStringsSign = "<";
			}
			else if (newVersionString == "-")
			{
				compareVersionStringsSign = ">";
			}
			else
			{
				auto comparisonResult = compareVersionStrings(oldVersionString, newVersionString);
				switch (comparisonResult)
				{
					case -1: compareVersionStringsSign = "<"; break;
					case  0: compareVersionStringsSign = "="; break;
					case  1: compareVersionStringsSign = ">"; break;
				}
			}

			string path;
			switch (actionType)
			{
				case InnerAction::Configure:
				{
					path = "**CONFIGURE**";
				}
				break;
				case InnerAction::Remove:
				{
					path = "**REMOVE**";
				}
				break;
				case InnerAction::Unpack:
				{
					path = archivesDirectory + "/" + _get_archive_basename(version);
				}
			}
			result += sf("%s %s %s %s %s\n", packageName.c_str(), oldVersionString.c_str(),
					compareVersionStringsSign.c_str(), newVersionString.c_str(), path.c_str());
		}
	}

	// strip last "\n", because apt-listchanges cannot live with it somewhy
	result.erase(result.end() - 1);

	return result;
}

void PackagesWorker::__do_dpkg_pre_packages_actions(const vector< vector< InnerAction > >& actionGroups)
{
	auto archivesDirectory = _get_archives_directory();
	auto commands = _config->getList("dpkg::pre-install-pkgs");
	FORIT(commandIt, commands)
	{
		string command = *commandIt;
		string commandBinary = command;

		auto spaceOffset = commandBinary.find(' ');
		if (spaceOffset != string::npos)
		{
			commandBinary.resize(spaceOffset);
		}

		string commandInput;
		auto versionOfInput = _config->getInteger(
				string("dpkg::tools::options::") + commandBinary + "::version");
		if (versionOfInput == 2)
		{
			commandInput = __generate_input_for_preinstall_v2_hooks(actionGroups);
		}
		else
		{
			// new debs are pulled to command through STDIN, one by line
			FORIT(actionGroupIt, actionGroups)
			{
				FORIT(actionIt, *actionGroupIt)
				{
					if (actionIt->type == InnerAction::Unpack)
					{
						auto debPath = archivesDirectory + "/" + _get_archive_basename(actionIt->version);
						commandInput += debPath;
						commandInput += "\n";
					}
				}
			}
		}

		string alias = command;
		command = string("echo '") + commandInput + "' | " + command;
		__run_dpkg_command("pre", command, alias);
	}
}

void PackagesWorker::__do_dpkg_post_actions()
{
	auto commands = _config->getList("dpkg::post-invoke");
	FORIT(commandIt, commands)
	{
		__run_dpkg_command("post", *commandIt, *commandIt);
	}
}

void PackagesWorker::__change_auto_status(InnerAction::Type actionType,
		const vector< InnerAction >& actionGroup)
{
	if (actionType == InnerAction::Configure)
	{
		return;
	}

	bool targetStatus = (actionType == InnerAction::Unpack); // will be false for removals

	const Resolver::SuggestedPackages& suggestedPackages =
			(*__actions_preview)[targetStatus ? Worker::Action::Markauto : Worker::Action::Unmarkauto];

	FORIT(actionIt, actionGroup)
	{
		const string& packageName = actionIt->version->packageName;
		if (suggestedPackages.count(packageName))
		{
			markAsAutomaticallyInstalled(packageName, targetStatus);
		}
	}
}

void PackagesWorker::markAsAutomaticallyInstalled(const string& packageName, bool targetStatus)
{
	auto simulating = _config->getBool("cupt::worker::simulate");

	if (simulating)
	{
		string prefix = targetStatus ?
					__("marking as automatically installed") : __("marking as manually installed");
		simulate("%s: %s", prefix.c_str(), packageName.c_str());
	}
	else
	{
		if (targetStatus)
		{
			__auto_installed_package_names.insert(packageName);
		}
		else
		{
			__auto_installed_package_names.erase(packageName);
		}
		auto extendedInfoPath = _cache->getPathOfExtendedStates();
		auto tempPath = extendedInfoPath + ".cupt.tmp";

		{
			string errorString;
			File tempFile(tempPath, "w", errorString);
			if (!errorString.empty())
			{
				fatal("unable to open temporary file '%s': %s", tempPath.c_str(), errorString.c_str());
			}

			// filling new info
			FORIT(packageNameIt, __auto_installed_package_names)
			{
				tempFile.put(sf("Package: %s\nAuto-Installed: 1\n\n", packageNameIt->c_str()));
			}
		}

		auto moveResult = fs::move(tempPath, extendedInfoPath);
		if (!moveResult.empty())
		{
			fatal("unable to renew extended states file: %s", moveResult.c_str());
		}
	}
}

void PackagesWorker::__do_downloads(const vector< pair< download::Manager::DownloadEntity, string > >& downloads,
		const shared_ptr< download::Progress >& downloadProgress)
{
	// don't bother ourselves with download preparings if nothing to download
	if (!downloads.empty())
	{
		auto archivesDirectory = _get_archives_directory();

		string downloadResult;
		{
			Lock lock(_config, archivesDirectory + "/lock");

			uint64_t totalDownloadSize = 0;
			FORIT(it, downloads)
			{
				totalDownloadSize += it->first.size;
			}
			downloadProgress->setTotalEstimatedSize(totalDownloadSize);

			vector< download::Manager::DownloadEntity > params;
			FORIT(it, downloads)
			{
				params.push_back(it->first);
			}

			{
				download::Manager downloadManager(_config, downloadProgress);
				downloadResult = downloadManager.download(params);
			} // make sure that download manager is already destroyed at this point, before lock is released
		}

		if (!downloadResult.empty())
		{
			fatal("there were download errors");
		}
	}
}

void PackagesWorker::changeSystem(const shared_ptr< download::Progress >& downloadProgress)
{
	auto debugging = _config->getBool("debug::worker");
	auto archivesSpaceLimit = _config->getInteger("cupt::worker::archives-space-limit");
	auto downloadOnly = _config->getBool("cupt::worker::download-only");

	auto preDownloads = __prepare_downloads();
	if (downloadOnly)
	{
		// download all now
		vector< pair< download::Manager::DownloadEntity, string > > downloads;
		FORIT(preDownloadIt, preDownloads)
		{
			downloads.push_back(preDownloadIt->second);
		}
		__do_downloads(downloads, downloadProgress);
		return;
	};

	vector< Changeset > changesets;
	{
		GraphAndAttributes gaa;
		if (!__build_actions_graph(gaa))
		{
			return; // exit when nothing to do
		}

		changesets = __get_changesets(gaa, preDownloads);
	}

	// doing or simulating the actions
	auto dpkgBinary = _config->getPath("dir::bin::dpkg");
	{
		auto dpkgOptions = _config->getList("dpkg::options");
		FORIT(optionIt, dpkgOptions)
		{
			dpkgBinary += " ";
			dpkgBinary += *optionIt;
		}
	}

	auto deferTriggers = _config->getBool("cupt::worker::defer-triggers");

	__do_dpkg_pre_actions();

	auto archivesDirectory = _get_archives_directory();
	auto purging = _config->getBool("cupt::worker::purge");
	FORIT(changesetIt, changesets)
	{
		const Changeset& changeset = *changesetIt;

		if (debugging)
		{
			debug("started changeset");
		}
		/* usually, all downloads are done before any install actions (first
		   and only changeset) however, if 'cupt::worker::archives-space-limit'
		   is turned on this is no longer the case, and we will do downloads/installs
		   by portions ("changesets") */
		__do_downloads(changeset.downloads, downloadProgress);

		__do_dpkg_pre_packages_actions(changeset.actionGroups);

		FORIT(actionGroupIt, changeset.actionGroups)
		{
			// all actions within one group have the same action name by algorithm
			InnerAction::Type actionType = (*actionGroupIt)[0].type;

			string actionName;
			switch (actionType)
			{
				case InnerAction::Remove:
					actionName = purging ? "purge" : "remove";
					break;
				case InnerAction::Unpack:
					actionName = "unpack";
					break;
				case InnerAction::Configure:
					actionName = "configure";
			}

			__change_auto_status(actionType, *actionGroupIt);

			{ // dpkg actions
				auto dpkgCommand = dpkgBinary + " --" + actionName;
				if (deferTriggers)
				{
					dpkgCommand += " --no-triggers";
				}
				// add necessary options if requested
				dpkgCommand += (*actionGroupIt)[0].dpkgFlags;

				/* the workaround for a dpkg bug #558151

				   dpkg performs some IMHO useless checks for programs in PATH
				   which breaks some upgrades of packages that contains important programs */
				dpkgCommand += " --force-bad-path";

				FORIT(actionIt, *actionGroupIt)
				{
					string actionExpression;
					if (actionType == InnerAction::Unpack)
					{
						const shared_ptr< const BinaryVersion > version = actionIt->version;
						actionExpression = archivesDirectory + '/' + _get_archive_basename(version);
					}
					else
					{
						actionExpression = actionIt->version->packageName;
					}
					dpkgCommand += " ";
					dpkgCommand += actionExpression;
				}
				if (debugging)
				{
					vector< string > stringifiedVersions;
					const string& dpkgFlags = (*actionGroupIt)[0].dpkgFlags;
					FORIT(actionIt, *actionGroupIt)
					{
						const shared_ptr< const BinaryVersion > version = actionIt->version;
						stringifiedVersions.push_back(version->packageName + '_' + version->versionString);
					}
					debug("%s %s %s", actionName.c_str(), dpkgFlags.c_str(),
							join(" ", stringifiedVersions).c_str());
				}
				_run_external_command(dpkgCommand);
			};
		}
		if (deferTriggers)
		{
			// triggers were not processed during actions perfomed before, do it now at once
			string command = dpkgBinary + " --triggers-only --pending";
			if (debugging)
			{
				debug("running triggers");
			}
			__run_dpkg_command("triggers", command, command);
		}

		if (archivesSpaceLimit)
		{
			__clean_downloads(changeset);
		}
		if (debugging)
		{
			debug("finished changeset");
		}
	}

	__do_dpkg_post_actions();
}

}
}


