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

set< string > __get_pseudo_essential_package_names(const Cache& cache, bool debugging)
{
	set< string > result;
	queue< shared_ptr< const BinaryVersion > > toProcess;

	auto processRelationExpression = [&cache, &result, &toProcess, &debugging](const RelationExpression& relationExpression)
	{
		auto satisfyingVersions = cache.getSatisfyingVersions(relationExpression);
		FORIT(satisfyingVersionIt, satisfyingVersions)
		{
			if ((*satisfyingVersionIt)->isInstalled())
			{
				if (result.insert((*satisfyingVersionIt)->packageName).second)
				{
					if (debugging)
					{
						debug("detected pseudo-essential package '%s'",
								(*satisfyingVersionIt)->packageName.c_str());
					}
					toProcess.push(*satisfyingVersionIt);
				}
			}
		}
	};

	{ // first wave - pre-depends only
		auto installedVersions = cache.getInstalledVersions();
		FORIT(versionIt, installedVersions)
		{
			if ((*versionIt)->essential)
			{
				FORIT(relationExpressionIt, (*versionIt)->relations[BinaryVersion::RelationTypes::PreDepends])
				{
					processRelationExpression(*relationExpressionIt);
				}
			}
		}
	}

	{ // processing the queue - pre-depends and depends
		while (!toProcess.empty())
		{
			auto version = toProcess.front();
			toProcess.pop();

			static const BinaryVersion::RelationTypes::Type relationTypes[] =
					{ BinaryVersion::RelationTypes::PreDepends, BinaryVersion::RelationTypes::Depends };
			for (size_t i = 0; i < sizeof(relationTypes)/sizeof(relationTypes[0]); ++i)
			{
				FORIT(relationExpressionIt, version->relations[relationTypes[i]])
				{
					processRelationExpression(*relationExpressionIt);
				}
			}
		}
	}

	return result;
}

void PackagesWorker::__fill_actions(GraphAndAttributes& gaa,
		vector< pair< InnerAction, InnerAction > >& basicEdges)
{
	typedef InnerAction IA;

	// user action - action name from actions preview
	const map< Action::Type, vector< IA::Type > > actionsMapping = {
		{ Action::Install, vector< IA::Type >{ IA::PriorityModifier, IA::Unpack, IA::Configure } },
		{ Action::Upgrade, vector< IA::Type >{ IA::PriorityModifier, IA::Remove, IA::Unpack, IA::Configure } },
		{ Action::Downgrade, vector< IA::Type >{ IA::PriorityModifier, IA::Remove, IA::Unpack, IA::Configure } },
		{ Action::Configure, vector< IA::Type >{ IA::Configure } },
		{ Action::Deconfigure, vector< IA::Type >{ IA::Remove } },
		{ Action::Remove, vector< IA::Type >{ IA::Remove } },
		{ Action::Purge, vector< IA::Type >{ IA::Remove } },
	};

	auto pseudoEssentialPackageNames = __get_pseudo_essential_package_names(
			*_cache, _config->getBool("debug::worker"));

	// convert all actions into inner ones
	FORIT(mapIt, actionsMapping)
	{
		const Action::Type& userAction = mapIt->first;
		const vector< IA::Type >& innerActionTypes = mapIt->second;

		FORIT(suggestedPackageIt, __actions_preview->groups[userAction])
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
				else if (innerActionType != IA::PriorityModifier)
				{
					version = suggestedPackageIt->second.version;
				}
				if (!version)
				{
					auto versionPtr = new BinaryVersion;
					versionPtr->packageName = packageName;
					versionPtr->versionString = "<dummy>";
					version.reset(versionPtr);
				}

				InnerAction action;
			    action.version = version;
				action.type = innerActionType;

				auto newVertexPtr = gaa.graph.addVertex(action);

				if (previousInnerActionPtr)
				{
					// the edge between consecutive actions
					using std::make_pair;
					basicEdges.push_back(make_pair(*previousInnerActionPtr, action));
					if (previousInnerActionPtr->type == IA::Remove &&
							(newVertexPtr->version->essential || pseudoEssentialPackageNames.count(packageName)))
					{
						// merging remove/unpack
						basicEdges.push_back(make_pair(action, *previousInnerActionPtr));
					}
					if (previousInnerActionPtr->type == IA::Unpack &&
							pseudoEssentialPackageNames.count(packageName))
					{
						// merging unpack/configure
						basicEdges.push_back(make_pair(action, *previousInnerActionPtr));
					}
				}
				previousInnerActionPtr = newVertexPtr;
			}
		}
	}
}

struct FillActionGeneralInfo
{
	shared_ptr< const Cache > cache;
	GraphAndAttributes* gaaPtr;
	bool debugging;
};

void __fill_action_dependencies(FillActionGeneralInfo& gi,
		const shared_ptr< const BinaryVersion >& version,
		BinaryVersion::RelationTypes::Type dependencyType, InnerAction::Type actionType,
		Direction::Type direction, const InnerAction& innerAction)
{
	const set< InnerAction >& verticesMap = gi.gaaPtr->graph.getVertices();

	InnerAction candidateAction;
	candidateAction.type = actionType;

	const RelationLine& relationLine = version->relations[dependencyType];
	FORIT(relationExpressionIt, relationLine)
	{
		auto satisfyingVersions = gi.cache->getSatisfyingVersions(*relationExpressionIt);

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

			// commented, because of #582423
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

			{ // checking if action dependency is neutralized by linked antagonistic action
				const InnerAction* antagonisticActionPtr = NULL;
				if (actionType == InnerAction::Configure && direction == Direction::Before)
				{
					if (currentAction.linkedFrom && currentAction.linkedFrom->linkedFrom)
					{
						antagonisticActionPtr = currentAction.linkedFrom->linkedFrom;
					}
				}
				else if (actionType == InnerAction::Remove && direction == Direction::After)
				{
					if (currentAction.linkedTo && currentAction.linkedTo->linkedTo)
					{
						antagonisticActionPtr = currentAction.linkedTo->linkedTo;
					}
				}

				if (antagonisticActionPtr)
				{
					auto predicate = std::bind2nd(PointerEqual< const BinaryVersion >(), antagonisticActionPtr->version);
					if (std::find_if(satisfyingVersions.begin(), satisfyingVersions.end(),
							predicate) != satisfyingVersions.end())
					{
						continue; // yes, neutralized
					}
				}
			}

			gi.gaaPtr->graph.addEdge(slaveAction, masterAction);

			// adding relation to attributes
			vector< GraphAndAttributes::RelationInfoRecord >& relationInfo =
					gi.gaaPtr->attributes[slaveAction][masterAction].relationInfo;
			GraphAndAttributes::RelationInfoRecord record =
					{ dependencyType, *relationExpressionIt, direction == Direction::After };
			relationInfo.push_back(std::move(record));

			if (gi.debugging)
			{
				debug("new action dependency: '%s' -> '%s', reason: '%s: %s'", slaveAction.toString().c_str(),
						masterAction.toString().c_str(), BinaryVersion::RelationTypes::rawStrings[dependencyType],
						relationExpressionIt->toString().c_str());
			}
		}
	}
}

void __fill_graph_dependencies(const shared_ptr< const Cache >& cache,
		GraphAndAttributes& gaa, bool debugging)
{
	typedef BinaryVersion::RelationTypes RT;

	FillActionGeneralInfo gi;
	gi.cache = cache;
	gi.gaaPtr = &gaa;
	gi.debugging = debugging;

	// fill the actions' dependencies
	const set< InnerAction >& vertices = gaa.graph.getVertices();
	FORIT(vertexIt, vertices)
	{
		const InnerAction& innerAction = *vertexIt;
		const shared_ptr< const BinaryVersion >& version = innerAction.version;
		switch (innerAction.type)
		{
			case InnerAction::PriorityModifier:
				break;
			case InnerAction::Unpack:
			{
				process_unpack:

				// pre-depends must be unpacked before
				__fill_action_dependencies(gi, version, RT::PreDepends, InnerAction::Configure,
						Direction::Before, innerAction);
				// conflicts must be unsatisfied before
				__fill_action_dependencies(gi, version, RT::Conflicts, InnerAction::Remove,
						Direction::Before, innerAction);
				// breaks must be unsatisfied before (yes, before the unpack)
				__fill_action_dependencies(gi, version, RT::Breaks, InnerAction::Remove,
						Direction::Before, innerAction);
			}
				break;
			case InnerAction::Configure:
			{
				// depends must be configured before
				__fill_action_dependencies(gi, version, RT::Depends, InnerAction::Configure,
						Direction::Before, innerAction);

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
				__fill_action_dependencies(gi, version, RT::PreDepends, InnerAction::Remove,
						Direction::After, innerAction);
				// depends must be removed after
				__fill_action_dependencies(gi, version, RT::Depends, InnerAction::Remove,
						Direction::After, innerAction);
				// conflicts may be satisfied only after
				__fill_action_dependencies(gi, version, RT::Conflicts, InnerAction::Unpack,
						Direction::After, innerAction);
				// breaks may be satisfied only after
				__fill_action_dependencies(gi, version, RT::Breaks, InnerAction::Unpack,
						Direction::After, innerAction);
				// in the previous case it may happen that package was already unpacked
				// with breaking dependencies already, so there won't be 'unpack' action but just
				// 'configure' one, so set dependency to 'configure' too just in case
				__fill_action_dependencies(gi, version, RT::Breaks, InnerAction::Configure,
						Direction::After, innerAction);
			}
		}
	}
}

void PackagesWorker::__check_graph_pre_depends(GraphAndAttributes& gaa, bool debugging)
{
	auto edges = gaa.graph.getEdges();
	FORIT(edgeIt, edges)
	{
		const InnerAction* fromPtr = edgeIt->first;
		const InnerAction* toPtr = edgeIt->second;
		const vector< GraphAndAttributes::RelationInfoRecord >& records =
				gaa.attributes[*fromPtr][*toPtr].relationInfo;

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
					fromPtr->toString().c_str(), toPtr->toString().c_str());
		}
		if (gaa.graph.getReachableFrom(*toPtr).count(fromPtr))
		{
			// bah! the pre-dependency cannot be overridden
			// it is not worker's fail (at least, it shouldn't be)

			auto path = gaa.graph.getPathVertices(*toPtr, *fromPtr);

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

vector< pair< InnerAction, InnerAction > > __create_virtual_actions(
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
				virtualEdges.push_back(std::make_pair(from, to));
			}
		}
	}

	return virtualEdges;
}

void __expand_and_delete_virtual_edges(GraphAndAttributes& gaa,
		const vector< pair< InnerAction, InnerAction > >& virtualEdges, bool debugging)
{
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

		// delete the whole attribute
		gaa.attributes[fromPredecessor].erase(fromSuccessor);

		gaa.graph.deleteEdge(fromPredecessor, fromSuccessor);
		gaa.graph.addEdge(toPredecessor, toSuccessor);
	};

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

struct ActionGroupPriority
{
	ssize_t main;
	size_t auxiliary;
};

ActionGroupPriority __get_action_group_priority(const vector< InnerAction >& preActionGroup)
{
	ssize_t mainPriority = 0;
	size_t auxiliaryPriority = 0;
	FORIT(actionIt, preActionGroup)
	{
		mainPriority += actionIt->priority;
		auxiliaryPriority += actionIt->type;
	}
	return ActionGroupPriority { mainPriority, auxiliaryPriority };
}
struct __action_group_pointer_priority_less
{
	bool operator()(const vector< InnerAction >* left, const vector< InnerAction >* right)
	{
		auto leftPriority = __get_action_group_priority(*left);
		auto rightPriority = __get_action_group_priority(*right);
		if (leftPriority.main < rightPriority.main)
		{
			return true;
		}
		else if (rightPriority.main < leftPriority.main)
		{
			return false;
		}
		else
		{
			return leftPriority.auxiliary < rightPriority.auxiliary;
		}
	}
};

void __for_each_package_sequence(const Graph< InnerAction >& graph,
		std::function< void (const InnerAction*, const InnerAction*) > callback)
{
	FORIT(innerActionIt, graph.getVertices())
	{
		if (innerActionIt->type == InnerAction::Unpack)
		{
			const string& packageName = innerActionIt->version->packageName;

			const InnerAction* fromPtr = &*innerActionIt;
			const InnerAction* toPtr = &*innerActionIt;

			const list< const InnerAction* >& predecessors = graph.getPredecessorsFromPointer(&*innerActionIt);
			FORIT(actionPtrIt, predecessors)
			{
				if ((*actionPtrIt)->type == InnerAction::Remove &&
					(*actionPtrIt)->version->packageName == packageName)
				{
					fromPtr = *actionPtrIt;
					break;
				}
			}

			const list< const InnerAction* >& successors = graph.getSuccessorsFromPointer(&*innerActionIt);
			FORIT(actionPtrIt, successors)
			{
				if ((*actionPtrIt)->type == InnerAction::Configure &&
					(*actionPtrIt)->version->packageName == packageName)
				{
					toPtr = *actionPtrIt;
					break;
				}
			}

			callback(fromPtr, toPtr);
		}
	}
}

void __set_action_priorities(GraphAndAttributes& gaa, bool debugging)
{
	auto adjustPair = [&gaa, &debugging](const InnerAction* fromPtr, const InnerAction* toPtr)
	{
		auto reachableToVertices = gaa.graph.getReachableTo(*toPtr);
		auto reachableFromVertices = gaa.graph.getReachableFrom(*fromPtr);
		FORIT(vertexPtrIt, reachableToVertices)
		{
			auto vertexPtr = *vertexPtrIt;
			if (reachableFromVertices.count(vertexPtr))
			{
				if (vertexPtr->type != InnerAction::PriorityModifier)
				{
					vertexPtr->priority += 1;
					if (debugging)
					{
						debug("adjusting action priority: '+1' for '%s'", vertexPtr->toString().c_str());
					}
				}
			}
			else
			{
				if (vertexPtr->type == InnerAction::PriorityModifier)
				{
					if (vertexPtr->version->packageName == fromPtr->version->packageName)
					{
						// this is "own" priority modifier, can be reached only once
						vertexPtr->priority = -(ssize_t)reachableToVertices.size();
						if (debugging)
						{
							debug("setting action priority: '%zd' for '%s'",
									vertexPtr->priority, vertexPtr->toString().c_str());
						}
					}
					else
					{
						// this is "other" priority modifier
						gaa.graph.addEdgeFromPointers(vertexPtr, fromPtr);
						if (debugging)
						{
							debug("setting priority link: '%s' -> '%s'",
									vertexPtr->toString().c_str(), fromPtr->toString().c_str());
						}
					}
				}
			}
		}
	};

	const set< InnerAction >& vertices = gaa.graph.getVertices();
	FORIT(innerActionIt, vertices)
	{
		innerActionIt->priority = 0;
	}
	__for_each_package_sequence(gaa.graph, adjustPair);
}

class __regular_action_group_insert_iterator
{
 public:
	typedef vector< vector< InnerAction > > container_t;
 private:
	container_t& __container;
 public:
	typedef __regular_action_group_insert_iterator self;
	__regular_action_group_insert_iterator(container_t& container)
		: __container(container)
	{}
	self& operator++()
	{
		return *this;
	}
	self& operator*()
	{
		return *this;
	}
	self& operator=(const vector< InnerAction >& data)
	{
		if (data.size() != 1 || data[0].type != InnerAction::PriorityModifier)
		{
			__container.push_back(data);
		}
		return *this;
	}
};

bool __link_actions(GraphAndAttributes& gaa, bool debugging)
{
	if (gaa.graph.getVertices().empty())
	{
		return false;
	}

	bool linkedSomething = false;

	__set_action_priorities(gaa, debugging);
	auto callback = [debugging](const vector< InnerAction >& preActionGroup, bool closing)
	{
		if (debugging)
		{
			vector< string > s;
			FORIT(actionIt, preActionGroup)
			{
				if (!actionIt->fake)
				{
					s.push_back(actionIt->toString());
				}
			}
			if (!s.empty())
			{
				auto priority = __get_action_group_priority(preActionGroup);
				debug("toposort: %s action group: '%s' (priority: %zd(%zu))",
						(closing ? "selected" : "opened"), join(", ", s).c_str(),
						priority.main, priority.auxiliary);
			}
		}
	};
	vector< vector< InnerAction > > preActionGroups;
	gaa.graph.topologicalSortOfStronglyConnectedComponents< __action_group_pointer_priority_less >
			(callback, __regular_action_group_insert_iterator(preActionGroups));

	auto processCandidates = [&gaa, &debugging, &linkedSomething](const InnerAction& from, const InnerAction& to)
	{
		if (to.linkedFrom || to.fake)
		{
			return; // was linked already
		}
		if (from.version->packageName == to.version->packageName)
		{
			if ((from.type == InnerAction::Remove && to.type == InnerAction::Unpack)
					|| (from.type == InnerAction::Unpack && to.type == InnerAction::Configure))
			{
				// both are surely existing vertices, getting only pointers
				const InnerAction* fromPtr = gaa.graph.addVertex(from);
				const InnerAction* toPtr = gaa.graph.addVertex(to);

				fromPtr->linkedTo = toPtr;
				toPtr->linkedFrom = fromPtr;
				linkedSomething = true;
				if (debugging)
				{
					debug("new link: '%s' -> '%s'",
							fromPtr->toString().c_str(), toPtr->toString().c_str());
				}
			}
		}
	};

	// contiguous action can be safely linked
	for(auto actionGroupIt = preActionGroups.begin();
			actionGroupIt != preActionGroups.end() - 1; ++actionGroupIt)
	{
		FORIT(actionIt, *actionGroupIt)
		{
			const InnerAction& from = *actionIt;
			if (from.linkedTo || from.fake)
			{
				continue; // was linked already
			}

			if (actionGroupIt->size() == 1)
			{
				// then, linked action should be also one in action group and a very next
				auto nextActionGroupIt = actionGroupIt + 1;
				if (nextActionGroupIt->size() == 1)
				{
					processCandidates(from, (*nextActionGroupIt)[0]);
				}
			}
			else
			{
				// linked action should be in the same group
				FORIT(candidateActionIt, *actionGroupIt)
				{
					processCandidates(from, *candidateActionIt);
				}
			}
		}
	}

	return linkedSomething;
}

void __make_cycles_for_linked_actions(GraphAndAttributes& gaa)
{
	const set< InnerAction >& vertices = gaa.graph.getVertices();
	FORIT(innerActionIt, vertices)
	{
		if (innerActionIt->type == InnerAction::Unpack)
		{
			if (innerActionIt->linkedFrom)
			{
				gaa.graph.addEdge(*innerActionIt, *(innerActionIt->linkedFrom));
			}

			if (innerActionIt->linkedTo)
			{
				gaa.graph.addEdge(*(innerActionIt->linkedTo), *innerActionIt);
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

	bool debugging = _config->getBool("debug::worker");

	{
		vector< pair< InnerAction, InnerAction > > basicEdges;
		__fill_actions(gaa, basicEdges);
		// maybe, we have nothing to do?
		if (gaa.graph.getVertices().empty() && __actions_preview->groups[Action::ProcessTriggers].empty())
		{
			return false;
		}
		auto virtualEdges = __create_virtual_actions(gaa, _cache);

		do
		{
			if (debugging)
			{
				debug("building action graph: next iteration");
			}
			gaa.graph.clearEdges();
			gaa.attributes.clear();

			FORIT(it, basicEdges)
			{
				gaa.graph.addEdge(it->first, it->second);
				gaa.attributes[it->first][it->second].isFundamental = true;
			}
			FORIT(it, virtualEdges)
			{
				gaa.graph.addVertex(it->first);
				gaa.graph.addVertex(it->second);
			}

			__fill_graph_dependencies(_cache, gaa, debugging);
			__expand_and_delete_virtual_edges(gaa, virtualEdges, debugging);
		} while (__link_actions(gaa, debugging));
		if (debugging)
		{
			debug("building action graph: finished");
		}

		__make_cycles_for_linked_actions(gaa);
	}

	if (debugging)
	{
		auto edges = gaa.graph.getEdges();
		FORIT(edgeIt, edges)
		{
			debug("the present action dependency: '%s' -> '%s', %s",
					edgeIt->first->toString().c_str(), edgeIt->second->toString().c_str(),
					gaa.attributes[*(edgeIt->first)][*(edgeIt->second)].isDependencyHard() ? "hard" : "soft");
		}
	}

	__check_graph_pre_depends(gaa, debugging);

	return true;
}

bool __is_circular_action_subgroup_allowed(const vector< InnerAction >& actionSubgroup)
{
	{ // do all actions have the same package name?
		const string& firstPackageName = actionSubgroup[0].version->packageName;
		bool samePackageName = true;
		FORIT(actionIt, actionSubgroup)
		{
			if (actionIt->version->packageName != firstPackageName)
			{
				samePackageName = false;
				break;
			}
		}
		if (samePackageName)
		{
			return true;
		}
	}

	// otherwise, only circular configures allowed
	FORIT(actionIt, actionSubgroup)
	{
		if (actionIt->type != InnerAction::Configure)
		{
			return false; // ooh, mixed circular dependency?
		}
	}

	return true;
}

vector< InnerActionGroup > __convert_vector(vector< vector< InnerAction > >&& source)
{
	vector< InnerActionGroup > result;
	FORIT(elemIt, source)
	{
		InnerActionGroup newElement;
		newElement.swap(*elemIt);
		result.push_back(std::move(newElement));
	}
	return result;
}

void __build_mini_action_graph(const shared_ptr< const Cache >& cache,
		const InnerActionGroup& actionGroup, GraphAndAttributes& gaa,
		GraphAndAttributes& miniGaa, bool debugging)
{
	using std::make_pair;
	vector< pair< const InnerAction*, const InnerAction* > > basicEdges;

	{ // filling minigraph and basic edges
		vector< pair< const InnerAction*, const InnerAction* > > possibleEdges;
		// fill vertices
		FORIT(actionIt, actionGroup)
		{
			const list< const InnerAction* >& successors = gaa.graph.getSuccessors(*actionIt);
			FORIT(successorPtrIt, successors)
			{
				possibleEdges.push_back(make_pair(&*actionIt, *successorPtrIt));
			}
			auto vertexPtr = miniGaa.graph.addVertex(*actionIt);
			vertexPtr->linkedFrom = NULL;
			vertexPtr->linkedTo = NULL;
		}
		// filtering edges
		const set< InnerAction >& allowedVertices = miniGaa.graph.getVertices();
		FORIT(edgeIt, possibleEdges)
		{
			const InnerAction& from = *(edgeIt->first);
			const InnerAction& to = *(edgeIt->second);
			if (!allowedVertices.count(to))
			{
				continue; // edge lies outside our mini graph
			}

			if (gaa.attributes[from][to].isFundamental)
			{
				basicEdges.push_back(*edgeIt);
				// also adding to the graph solely for next priority modifiers block
				miniGaa.graph.addEdge(from, to);
			}
		}
		{ // adding priority modifiers
			vector< pair< const InnerAction*, const InnerAction* > > sequences;
			__for_each_package_sequence(miniGaa.graph,
					[&sequences](const InnerAction* fromPtr, const InnerAction* toPtr)
					{
						sequences.push_back(make_pair(fromPtr, toPtr));
					});
			FORIT(sequenceIt, sequences)
			{
				const InnerAction* fromPtr = sequenceIt->first;
				const InnerAction* toPtr = sequenceIt->second;
				if (fromPtr != toPtr) // if sequence has at least two elements
				{
					InnerAction priorityModifier = *toPtr;
					priorityModifier.type = InnerAction::PriorityModifier;
					auto newVertex = miniGaa.graph.addVertex(priorityModifier);
					basicEdges.push_back(make_pair(newVertex, fromPtr));
				}
			}
		}
	}
	do // iterating
	{
		if (debugging)
		{
			debug("building mini action graph: next iteration");
		}
		miniGaa.graph.clearEdges();
		miniGaa.attributes.clear();
		FORIT(it, basicEdges)
		{
			miniGaa.graph.addEdge(*(it->first), *(it->second));
			miniGaa.attributes[*(it->first)][*(it->second)].isFundamental = true;
		}

		__fill_graph_dependencies(cache, miniGaa, debugging);
		{ // deleting soft edges
			auto edges = miniGaa.graph.getEdges();
			FORIT(edgeIt, edges)
			{
				const InnerAction& from = *(edgeIt->first);
				const InnerAction& to = *(edgeIt->second);
				if (!miniGaa.attributes[from][to].isDependencyHard())
				{
					miniGaa.graph.deleteEdge(from, to);
					if (debugging)
					{
						debug("ignoring soft edge '%s' -> '%s'", from.toString().c_str(), to.toString().c_str());
					}
				}
			}
		}
	} while (__link_actions(miniGaa, debugging));
	__make_cycles_for_linked_actions(miniGaa);
	if (debugging)
	{
		debug("building mini action graph: finished");
	}
}

void __split_heterogeneous_actions(const shared_ptr< const Cache >& cache,
		vector< InnerActionGroup >& actionGroups, GraphAndAttributes& gaa, bool debugging)
{
	auto dummyCallback = [](const vector< InnerAction >&, bool) {};

	vector< InnerActionGroup > newActionGroups;

	FORIT(actionGroupIt, actionGroups)
	{
		const InnerActionGroup& actionGroup = *actionGroupIt;
		if (actionGroup.size() > 1)
		{
			// multiple actions at once

			// we build a mini-graph with really important edges
			GraphAndAttributes miniGaa;
			__build_mini_action_graph(cache, actionGroup, gaa, miniGaa, debugging);

			vector< InnerActionGroup > actionSubgroupsSorted;
			{
				vector< vector< InnerAction > > preActionSubgroups;
				miniGaa.graph.topologicalSortOfStronglyConnectedComponents< PointerLess< vector< InnerAction > > >
						(dummyCallback, __regular_action_group_insert_iterator(preActionSubgroups));
				actionSubgroupsSorted = __convert_vector(std::move(preActionSubgroups));
			}
			FORIT(actionSubgroupIt, actionSubgroupsSorted)
			{
				InnerActionGroup& actionSubgroup = *actionSubgroupIt;
				if (actionSubgroup.size() > 1)
				{
					if (!__is_circular_action_subgroup_allowed(actionSubgroup))
					{
						// no-go
						vector< string > actionStrings;
						FORIT(it, actionSubgroup)
						{
							actionStrings.push_back(it->toString());
						}
						fatal("internal error: unable to schedule circular actions '%s'", join(", ", actionStrings).c_str());
					}
				}

				if (actionSubgroupsSorted.size() > 1) // self-contained heterogeneous actions don't need it
				{
					// always set forcing all dependencies
					// dpkg requires to pass both --force-depends and --force-breaks to achieve it
					actionSubgroup.dpkgFlags = " --force-depends --force-breaks";
				}
				actionSubgroup.continued = true;

				newActionGroups.push_back(actionSubgroup);
			}
			newActionGroups.rbegin()->continued = false;
		}
		else
		{
			newActionGroups.push_back(actionGroup);
		}
	}
	FORIT(groupIt, newActionGroups)
	{
		static auto comparator = [](const InnerAction& left, const InnerAction& right)
		{
			return left.type < right.type;
		};
		std::stable_sort(groupIt->begin(), groupIt->end(), comparator);
		if (debugging)
		{
			vector< string > strings;
			FORIT(it, *groupIt)
			{
				strings.push_back(it->toString());
			}
			debug("split action group: %s", join(", ", strings).c_str());
		}
	}

	newActionGroups.swap(actionGroups);
}

static string __get_codename_and_component_string(const Version& version)
{
	vector< string > parts;
	FORIT(sourceIt, version.sources)
	{
		auto releaseInfo = sourceIt->release;
		if (releaseInfo->baseUri.empty())
		{
			continue;
		}
		parts.push_back(releaseInfo->codename + '/' + releaseInfo->component);
	}
	return join(",", parts);
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
		const Resolver::SuggestedPackages& suggestedPackages = __actions_preview->groups[*actionIt];
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

			string longAliasTail = sf("%s %s %s", __get_codename_and_component_string(*version).c_str(),
						packageName.c_str(), versionString.c_str());
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
		const vector< InnerActionGroup >& actionGroups,
		const map< string, pair< download::Manager::DownloadEntity, string > >& downloads)
{
	vector< Changeset > result;
	Changeset changeset;
	set< string > unpackedPackageNames;

	FORIT(actionGroupIt, actionGroups)
	{
		FORIT(actionIt, *actionGroupIt)
		{
			auto actionType = actionIt->type;
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

		if (unpackedPackageNames.empty() && !actionGroupIt->continued)
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

	vector< Changeset > changesets;

	vector< InnerActionGroup > actionGroups;
	{
		auto dummyCallback = [](const vector< InnerAction >&, bool) {};
		vector< vector< InnerAction > > preActionGroups;
		gaa.graph.topologicalSortOfStronglyConnectedComponents< __action_group_pointer_priority_less >
				(dummyCallback, __regular_action_group_insert_iterator(preActionGroups));
		actionGroups = __convert_vector(std::move(preActionGroups));
	}
	__split_heterogeneous_actions(_cache, actionGroups, gaa, debugging);

	changesets = __split_action_groups_into_changesets(actionGroups, downloads);

	if (archivesSpaceLimit)
	{
		auto maxDownloadAmount = __get_max_download_amount(changesets, debugging);
		if (debugging)
		{
			debug("the changeset download amounts: maximum: %s",
					humanReadableSizeString(maxDownloadAmount).c_str());
		}
		if (maxDownloadAmount > archivesSpaceLimit)
		{
			// we failed to fit in limit
			fatal("unable to fit in archives space limit '%zu', best try is '%zu'",
					archivesSpaceLimit, maxDownloadAmount);
		}
	}

	{ // merging changesets
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
		const vector< InnerActionGroup >& actionGroups)
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
		FORIT(actionIt, *actionGroupIt)
		{
			auto actionType = actionIt->type;
			const shared_ptr< const BinaryVersion >& version = actionIt->version;
			string path;
			switch (actionType)
			{
				case InnerAction::PriorityModifier:
				continue;
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

			result += sf("%s %s %s %s %s\n", packageName.c_str(), oldVersionString.c_str(),
					compareVersionStringsSign.c_str(), newVersionString.c_str(), path.c_str());
		}
	}

	// strip last "\n", because apt-listchanges cannot live with it somewhy
	result.erase(result.end() - 1);

	return result;
}

void PackagesWorker::__do_dpkg_pre_packages_actions(const vector< InnerActionGroup >& actionGroups)
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

void PackagesWorker::__change_auto_status(const InnerActionGroup& actionGroup)
{
	FORIT(actionIt, actionGroup)
	{
		auto actionType = actionIt->type;
		if (actionType == InnerAction::Configure)
		{
			continue;
		}

		bool targetStatus = (actionType == InnerAction::Unpack); // will be false for removals

		const map< string, bool >& autoFlagChanges = __actions_preview->autoFlagChanges;

		const string& packageName = actionIt->version->packageName;
		auto it = autoFlagChanges.find(packageName);
		if (it != autoFlagChanges.end() && it->second == targetStatus)
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

	{ // make sure system is trigger-clean
		auto command = dpkgBinary + " --triggers-only -a";
		__run_dpkg_command("triggers-only", command, command);
	}

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
			/* all actions within one group have, by algorithm,
			   a) the same action name (so far, configures only)
			   b) the same package name (linked actions)

			   in both cases, we can choose the action type of the last action
			   in the subgroup as effective
			*/
			InnerAction::Type actionType = actionGroupIt->rbegin()->type;

			string actionName;
			switch (actionType)
			{
				case InnerAction::PriorityModifier:
					fatal("internal error: unexpected priority-modifier action in the changeset");
					break;
				case InnerAction::Remove:
					actionName = purging ? "purge" : "remove";
					break;
				case InnerAction::Unpack:
					actionName = "unpack";
					break;
				case InnerAction::Configure:
					if (actionGroupIt->size() >= 2 && (actionGroupIt->rbegin() + 1)->type == InnerAction::Unpack)
					{
						actionName = "install"; // [remove+]unpack+configure
					}
					else
					{
						actionName = "configure";
					}
					break;
			}

			__change_auto_status(*actionGroupIt);

			{ // dpkg actions
				auto dpkgCommand = dpkgBinary + " --" + actionName;
				if (deferTriggers)
				{
					dpkgCommand += " --no-triggers";
				}
				// add necessary options if requested
				dpkgCommand += actionGroupIt->dpkgFlags;

				// TODO: re-evaluate with lenny->squeeze (e)glibc upgrade
				/* the workaround for a dpkg bug #558151

				   dpkg performs some IMHO useless checks for programs in PATH
				   which breaks some upgrades of packages that contains important programs */
				dpkgCommand += " --force-bad-path";

				FORIT(actionIt, *actionGroupIt)
				{
					if (actionIt->type != actionType)
					{
						continue; // will be true for non-last linked actions
					}
					string actionExpression;
					if (actionName == "unpack" || actionName == "install")
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

					vector< string > stringifiedActions;
					const string& dpkgFlags = actionGroupIt->dpkgFlags;
					FORIT(actionIt, *actionGroupIt)
					{
						stringifiedActions.push_back(actionIt->toString());
					}
					debug("do: (%s) %s%s", join(" & ", stringifiedActions).c_str(),
							dpkgFlags.c_str(), actionGroupIt->continued ? " (continued)" : "");
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

