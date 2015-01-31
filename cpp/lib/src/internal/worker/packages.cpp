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
#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/releaseinfo.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/system/state.hpp>
#include <cupt/file.hpp>
#include <cupt/download/progress.hpp>
#include <cupt/versionstring.hpp>

#include <internal/filesystem.hpp>
#include <internal/debdeltahelper.hpp>
#include <internal/lock.hpp>
#include <internal/cachefiles.hpp>

#include <internal/worker/packages.hpp>
#include <internal/worker/dpkg.hpp>

#include <unistd.h>

namespace cupt {
namespace internal {

// InnerAction
InnerAction::InnerAction()
	: fake(false), priority(0), linkedFrom(NULL), linkedTo(NULL)
{}

bool InnerAction::operator<(const InnerAction& other) const
{
	if (type < other.type)
	{
		return true;
	}
	else if (type > other.type)
	{
		return false;
	}
	else
	{
		return *version < *(other.version);
	}
}

string InnerAction::toString() const
{
	const static string typeStrings[] = { "remove", "unpack", "configure", };
	string prefix = fake ? "(fake)" : "";
	string result = prefix + typeStrings[type] + " " + version->packageName +
			" " + version->versionString;

	return result;
}

InnerAction::Type InnerActionGroup::getCompoundActionType() const
{
	/* all actions within one group have, by algorithm,
	   a) the same action name (so far, configures only)
	   b) the same package name (linked actions)

	   in both cases, we can choose the action type of the last action
	   in the subgroup as effective
	*/
	return this->rbegin()->type;
}


// Attribute
GraphAndAttributes::Attribute::Attribute()
	: isFundamental(false)
{}

auto GraphAndAttributes::Attribute::getLevel() const -> Level
{
	if (isFundamental)
	{
		return Fundamental;
	}

	Level result = Priority; // by default
	FORIT(recordIt, relationInfo)
	{
		Level subLevel;
		if (recordIt->dependencyType == BinaryVersion::RelationTypes::Conflicts)
		{
			subLevel = Hard;
		}
		else if (recordIt->dependencyType == BinaryVersion::RelationTypes::Breaks)
		{
			subLevel = Medium;
		}
		else if (recordIt->fromVirtual)
		{
			subLevel = FromVirtual;
		}
		else if (recordIt->reverse)
		{
			subLevel = Soft;
		}
		else
		{
			subLevel = Hard;
		}
		result = std::max(result, subLevel);
	}

	return result;
};

const char* GraphAndAttributes::Attribute::levelStrings[] = {
	"priority", "from-virtual", "soft", "medium", "hard", "fundamental"
};


using std::make_pair;

typedef Graph< InnerAction >::CessorListType GraphCessorListType;

PackagesWorker::PackagesWorker()
{
	p_actualExtendedInfo = _cache->getExtendedInfo().raw;
}

set< string > __get_pseudo_essential_package_names(const Cache& cache, bool debugging)
{
	set< string > result;
	queue< const BinaryVersion* > toProcess;

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
						debug2("detected pseudo-essential package '%s'", (*satisfyingVersionIt)->packageName);
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

const BinaryVersion* PackagesWorker::__get_fake_version_for_purge(const string& packageName)
{
	auto& versionPtr = __fake_versions_for_purge[packageName];
	if (!versionPtr)
	{
		versionPtr.reset(new BinaryVersion);
		versionPtr->packageName = packageName;
		versionPtr->versionString = "<dummy>";
		versionPtr->essential = false;
	}

	return versionPtr.get();
}

void __set_action_priority(const InnerAction* actionPtr, const InnerAction* previousActionPtr)
{
	/* priorities are assigned that way so the possible chains are sorted in
	 * the following order by total priority, from the worst to the best:
	 *
	 * remove
	 * unpack
	 * remove + unpack-after-removal
	 * unpack + configure
	 * remove + unpack-after-removal + configure
	 * configure
	 * unpack-after-removal
	 * unpack-after-removal + configure
	 */
	switch (actionPtr->type)
	{
		case InnerAction::Remove:
			actionPtr->priority = -5;
			break;
		case InnerAction::Unpack:
			if (previousActionPtr)
			{
				actionPtr->priority = 4; // unpack-after-removal
			}
			else
			{
				actionPtr->priority = -2;
			}
			break;
		case InnerAction::Configure:
			actionPtr->priority = 3;
			break;
	};
}

void PackagesWorker::__fill_actions(GraphAndAttributes& gaa)
{
	typedef InnerAction IA;

	// user action - action name from actions preview
	const map< Action::Type, vector< IA::Type > > actionsMapping = {
		{ Action::Install, vector< IA::Type >{ IA::Unpack, IA::Configure } },
		{ Action::Upgrade, vector< IA::Type >{ IA::Remove, IA::Unpack, IA::Configure } },
		{ Action::Downgrade, vector< IA::Type >{ IA::Remove, IA::Unpack, IA::Configure } },
		{ Action::Reinstall, vector< IA::Type >{ IA::Remove, IA::Unpack, IA::Configure } },
		{ Action::Configure, vector< IA::Type >{ IA::Configure } },
		{ Action::Deconfigure, vector< IA::Type >{ IA::Remove } },
		{ Action::Remove, vector< IA::Type >{ IA::Remove } },
		{ Action::Purge, vector< IA::Type >{ IA::Remove } },
	};

	auto pseudoEssentialPackageNames = __get_pseudo_essential_package_names(
			*_cache, _config->getBool("debug::worker"));

	auto addBasicEdge = [&gaa](const InnerAction* fromPtr, const InnerAction* toPtr)
	{
		gaa.graph.addEdge(fromPtr, toPtr);
		gaa.attributes[make_pair(fromPtr, toPtr)].isFundamental = true;
	};

	// convert all actions into inner ones
	FORIT(mapIt, actionsMapping)
	{
		const Action::Type& userAction = mapIt->first;
		const vector< IA::Type >& innerActionTypes = mapIt->second;

		string userActionString = string("task: ") + Action::rawStrings[userAction];

		FORIT(suggestedPackageIt, __actions_preview->groups[userAction])
		{
			const string& packageName = suggestedPackageIt->first;

			const InnerAction* previousInnerActionPtr = NULL;

			string logMessage = userActionString + ' ' + packageName + " [";

			FORIT(innerActionTypeIt, innerActionTypes)
			{
				const IA::Type& innerActionType = *innerActionTypeIt;

				const BinaryVersion* version = nullptr;
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
					version = __get_fake_version_for_purge(packageName);
				}

				if (innerActionType != IA::Unpack)
				{
					if (*logMessage.rbegin() != '[') // upgrade/downgrade
					{
						logMessage += " -> ";
					}
					logMessage += version->versionString;
				}

				InnerAction action;
			    action.version = version;
				action.type = innerActionType;

				auto newVertexPtr = gaa.graph.addVertex(action);
				__set_action_priority(newVertexPtr, previousInnerActionPtr);

				if (previousInnerActionPtr)
				{
					// the edge between consecutive actions
					using std::make_pair;
					addBasicEdge(previousInnerActionPtr, newVertexPtr);
					if (previousInnerActionPtr->type == IA::Remove &&
							(newVertexPtr->version->essential || pseudoEssentialPackageNames.count(packageName)))
					{
						// merging remove/unpack
						addBasicEdge(newVertexPtr, previousInnerActionPtr);
					}
					if (previousInnerActionPtr->type == IA::Unpack &&
							pseudoEssentialPackageNames.count(packageName))
					{
						// merging unpack/configure
						addBasicEdge(newVertexPtr, previousInnerActionPtr);
					}
				}
				previousInnerActionPtr = newVertexPtr;
			}

			logMessage += "]";
			_logger->log(Logger::Subsystem::Packages, 2, logMessage);
		}
	}
}

struct FillActionGeneralInfo
{
	shared_ptr< const Cache > cache;
	GraphAndAttributes* gaaPtr;
	bool debugging;
	const InnerAction* innerActionPtr;
};
struct Direction
{
	enum Type { After, Before };
};

void __fill_action_dependencies(FillActionGeneralInfo& gi,
		BinaryVersion::RelationTypes::Type dependencyType, InnerAction::Type actionType,
		Direction::Type direction)
{
	typedef BinaryVersion::RelationTypes RT;
	if (gi.innerActionPtr->fake &&
			(dependencyType != RT::PreDepends && dependencyType != RT::Depends))
	{
		return;
	}

	const set< InnerAction >& verticesMap = gi.gaaPtr->graph.getVertices();

	InnerAction candidateAction;
	candidateAction.type = actionType;

	const RelationLine& relationLine = gi.innerActionPtr->version->relations[dependencyType];
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
			const InnerAction* currentActionPtr = &*vertexIt;
			if (gi.innerActionPtr->fake && currentActionPtr->fake)
			{
				continue;
			}

			auto masterActionPtr = (direction == Direction::After ? currentActionPtr : gi.innerActionPtr);
			auto slaveActionPtr = (direction == Direction::After ? gi.innerActionPtr : currentActionPtr);

			gi.gaaPtr->graph.addEdge(slaveActionPtr, masterActionPtr);

			bool fromVirtual = slaveActionPtr->fake || masterActionPtr->fake;
			// adding relation to attributes
			vector< GraphAndAttributes::RelationInfoRecord >& relationInfo =
					gi.gaaPtr->attributes[make_pair(slaveActionPtr, masterActionPtr)].relationInfo;
			GraphAndAttributes::RelationInfoRecord record =
					{ dependencyType, *relationExpressionIt, direction == Direction::After, fromVirtual };
			relationInfo.push_back(std::move(record));

			if (gi.debugging)
			{
				debug2("new action dependency: '%s' -> '%s', reason: '%s: %s'", slaveActionPtr->toString(),
						masterActionPtr->toString(), BinaryVersion::RelationTypes::rawStrings[dependencyType],
						relationExpressionIt->toString());
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
		const InnerAction* innerActionPtr = &*vertexIt;
		gi.innerActionPtr = innerActionPtr;
		switch (innerActionPtr->type)
		{
			case InnerAction::Unpack:
			{
				process_unpack:

				// pre-depends must be unpacked before
				__fill_action_dependencies(gi, RT::PreDepends, InnerAction::Configure, Direction::Before);
				// conflicts must be unsatisfied before
				__fill_action_dependencies(gi, RT::Conflicts, InnerAction::Remove, Direction::Before);
				// breaks must be unsatisfied before (yes, before the unpack)
				__fill_action_dependencies(gi, RT::Breaks, InnerAction::Remove, Direction::Before);
			}
				break;
			case InnerAction::Configure:
			{
				// depends must be configured before
				__fill_action_dependencies(gi, RT::Depends, InnerAction::Configure, Direction::Before);

				// it has also to be unpacked if the same version was not in state 'unpacked'
				// search for the appropriate unpack action
				auto candidateAction = *innerActionPtr;
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
				__fill_action_dependencies(gi, RT::PreDepends, InnerAction::Remove, Direction::After);
				// depends must be removed after
				__fill_action_dependencies(gi, RT::Depends, InnerAction::Remove, Direction::After);
				// conflicts may be satisfied only after
				__fill_action_dependencies(gi, RT::Conflicts, InnerAction::Unpack, Direction::After);
				// breaks may be satisfied only after
				__fill_action_dependencies(gi, RT::Breaks, InnerAction::Unpack, Direction::After);
				// in the previous case it may happen that package was already unpacked
				// with breaking dependencies already, so there won't be 'unpack' action but just
				// 'configure' one, so set dependency to 'configure' too just in case
				__fill_action_dependencies(gi, RT::Breaks, InnerAction::Configure, Direction::After);
			}
		}
	}
}

void __create_virtual_edge(
		const BinaryVersion* fromVirtualVersion,
		const BinaryVersion* toVirtualVersion,
		vector< pair< InnerAction, InnerAction > >* virtualEdgesPtr)
{
	InnerAction from;
	from.version = toVirtualVersion;
	from.type = InnerAction::Configure;
	from.fake = true;

	InnerAction to;
	to.version = fromVirtualVersion;
	to.type = InnerAction::Remove;
	to.fake = true;

	virtualEdgesPtr->push_back(std::make_pair(from, to));
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
	*/
	vector< pair< InnerAction, InnerAction > > virtualEdges;

	set< string > blacklistedPackageNames;
	// building the black list
	FORIT(vertexIt, gaa.graph.getVertices())
	{
		blacklistedPackageNames.insert(vertexIt->version->packageName);
	}

	auto installedVersions = cache->getInstalledVersions();
	for (const auto& installedVersion: installedVersions)
	{
		const string& packageName = installedVersion->packageName;
		if (blacklistedPackageNames.count(packageName))
		{
			continue;
		}

		__create_virtual_edge(installedVersion, installedVersion, &virtualEdges);
	}

	return virtualEdges;
}

const GraphAndAttributes::RelationInfoRecord* __get_shared_relation_info_record(
		const GraphAndAttributes::Attribute& left, const GraphAndAttributes::Attribute& right)
{
	for (const auto& leftRelationRecord: left.relationInfo)
	{
		for (const auto& rightRelationRecord: right.relationInfo)
		{
			if (leftRelationRecord.dependencyType == rightRelationRecord.dependencyType &&
					leftRelationRecord.relationExpression == rightRelationRecord.relationExpression)
			{
				return &leftRelationRecord;
			}
		}
	}
	return nullptr;
}

void __for_each_package_sequence(const Graph< InnerAction >& graph,
		std::function< void (const InnerAction*, const InnerAction*, const InnerAction*) > callback)
{
	FORIT(innerActionIt, graph.getVertices())
	{
		if (innerActionIt->type == InnerAction::Unpack)
		{
			const string& packageName = innerActionIt->version->packageName;

			const InnerAction* fromPtr = &*innerActionIt;
			const InnerAction* toPtr = &*innerActionIt;

			const GraphCessorListType& predecessors = graph.getPredecessors(&*innerActionIt);
			FORIT(actionPtrIt, predecessors)
			{
				if ((*actionPtrIt)->type == InnerAction::Remove &&
					(*actionPtrIt)->version->packageName == packageName)
				{
					fromPtr = *actionPtrIt;
					break;
				}
			}

			const GraphCessorListType& successors = graph.getSuccessors(&*innerActionIt);
			FORIT(actionPtrIt, successors)
			{
				if ((*actionPtrIt)->type == InnerAction::Configure &&
					(*actionPtrIt)->version->packageName == packageName)
				{
					toPtr = *actionPtrIt;
					break;
				}
			}

			callback(fromPtr, toPtr, &*innerActionIt);
		}
	}
}

void __move_edge(GraphAndAttributes& gaa,
		const InnerAction* fromPredecessorPtr, const InnerAction* fromSuccessorPtr,
		const InnerAction* toPredecessorPtr, const InnerAction* toSuccessorPtr,
		bool debugging)
{
	if (debugging)
	{
		debug2("moving edge '%s' -> '%s' to edge '%s' -> '%s'",
				fromPredecessorPtr->toString(), fromSuccessorPtr->toString(),
				toPredecessorPtr->toString(), toSuccessorPtr->toString());
	}

	GraphAndAttributes::Attribute& toAttribute = gaa.attributes[make_pair(toPredecessorPtr, toSuccessorPtr)];
	GraphAndAttributes::Attribute& fromAttribute = gaa.attributes[make_pair(fromPredecessorPtr, fromSuccessorPtr)];

	// concatenating relationInfo
	FORIT(relationRecordIt, fromAttribute.relationInfo)
	{
		toAttribute.relationInfo.push_back(std::move(*relationRecordIt));
	}
	// delete the whole attribute
	gaa.attributes.erase(make_pair(fromPredecessorPtr, fromSuccessorPtr));

	// edge 'fromPredecessorPtr' -> 'fromSuccessorPtr' should be deleted
	// manually after the call of this function

	gaa.graph.addEdge(toPredecessorPtr, toSuccessorPtr);
};

void __expand_and_delete_virtual_edges(GraphAndAttributes& gaa,
		const vector< pair< InnerAction, InnerAction > >& virtualEdges, bool debugging)
{
	FORIT(edgeIt, virtualEdges)
	{
		// getting vertex pointers
		const InnerAction* fromPtr = gaa.graph.addVertex(edgeIt->first);
		const InnerAction* toPtr = gaa.graph.addVertex(edgeIt->second);

		// "multiplying" the dependencies
		const auto& predecessors = gaa.graph.getPredecessors(fromPtr);
		const auto& successors = gaa.graph.getSuccessors(toPtr);
		for (auto predecessor: predecessors)
		{
			for (auto successor: successors)
			{
				if (predecessor == successor) continue;

				auto sharedRir = __get_shared_relation_info_record(
						gaa.attributes[make_pair(predecessor, fromPtr)],
						gaa.attributes[make_pair(toPtr, successor)]);
				if (!sharedRir) continue;

				gaa.graph.addEdge(predecessor, successor);
				gaa.attributes[make_pair(predecessor, successor)].relationInfo.push_back(*sharedRir);
				if (debugging)
				{
					const string& mediatorPackageName = fromPtr->version->packageName;
					debug2("multiplied action dependency: '%s' -> '%s', virtual mediator: '%s'",
							predecessor->toString(), successor->toString(), mediatorPackageName);
				}
			}
		}
		for (auto predecessor: predecessors)
		{
			gaa.attributes.erase(make_pair(predecessor, fromPtr));
		}
		for (auto successor: successors)
		{
			gaa.attributes.erase(make_pair(toPtr, successor));
		}
		gaa.graph.deleteVertex(*fromPtr);
		gaa.graph.deleteVertex(*toPtr);
	}
}

void __expand_linked_actions(const Cache& cache, GraphAndAttributes& gaa, bool debugging)
{
	auto canBecomeVirtual = [&cache](const GraphAndAttributes::Attribute& attribute,
			const InnerAction* antagonisticPtr, bool neededValueOfReverse)
	{
		auto attributeLevel = attribute.getLevel();
		if (attributeLevel == GraphAndAttributes::Attribute::Level::Priority)
		{
			return false; // not an interesting edge
		}
		if (attributeLevel == GraphAndAttributes::Attribute::Level::Fundamental)
		{
			return false; // unmoveable
		}
		FORIT(relationRecordIt, attribute.relationInfo)
		{
			if (relationRecordIt->reverse == neededValueOfReverse)
			{
				auto satisfyingVersions = cache.getSatisfyingVersions(relationRecordIt->relationExpression);
				if (std::find(satisfyingVersions.begin(), satisfyingVersions.end(),
						antagonisticPtr->version) == satisfyingVersions.end())
				{
					return false;
				}
			}
		}
		return true;
	};

	auto setVirtual = [&cache](GraphAndAttributes::Attribute& attribute)
	{
		FORIT(relationRecordIt, attribute.relationInfo)
		{
			relationRecordIt->fromVirtual = true; // this relation record is only virtual now
		}
	};

	auto moveEdgeToPotential = [&gaa, &setVirtual, debugging](
		const InnerAction* fromPredecessorPtr, const InnerAction* fromSuccessorPtr,
		GraphAndAttributes::Attribute& fromAttribute,
		const InnerAction* toPredecessorPtr, const InnerAction* toSuccessorPtr)
	{
		gaa.potentialEdges.insert(make_pair(
				InnerActionPtrPair(toPredecessorPtr, toSuccessorPtr),
				make_pair(InnerActionPtrPair(fromPredecessorPtr, fromSuccessorPtr), fromAttribute)));

		setVirtual(fromAttribute);
		__move_edge(gaa, fromPredecessorPtr, fromSuccessorPtr, toPredecessorPtr, toSuccessorPtr, debugging);

		gaa.graph.deleteEdge(fromPredecessorPtr, fromSuccessorPtr);
		if (debugging)
		{
			debug2("deleting the edge '%s' -> '%s'",
					fromPredecessorPtr->toString(), fromSuccessorPtr->toString());
		}
	};

	__for_each_package_sequence(gaa.graph, [&gaa, &canBecomeVirtual, &moveEdgeToPotential]
			(const InnerAction* fromPtr, const InnerAction* toPtr, const InnerAction*)
			{
				if (fromPtr->type != InnerAction::Remove || toPtr->type != InnerAction::Configure)
				{
					return; // we are dealing only with full chains
				}
				if (!fromPtr->linkedTo || fromPtr->linkedTo != toPtr->linkedFrom)
				{
					return; // this chain is not fully linked
				}

				const GraphCessorListType predecessors = gaa.graph.getPredecessors(fromPtr); // copying
				FORIT(predecessorPtrIt, predecessors)
				{
					if (*predecessorPtrIt == toPtr)
					{
						continue;
					}
					GraphAndAttributes::Attribute& attribute = gaa.attributes[make_pair(*predecessorPtrIt, fromPtr)];
					if (canBecomeVirtual(attribute, toPtr, true))
					{
						moveEdgeToPotential(*predecessorPtrIt, fromPtr, attribute, toPtr, fromPtr);
					}
				}

				const GraphCessorListType successors = gaa.graph.getSuccessors(toPtr);
				FORIT(successorPtrIt, successors)
				{
					if (*successorPtrIt == fromPtr)
					{
						continue;
					}
					GraphAndAttributes::Attribute& attribute = gaa.attributes[make_pair(toPtr, *successorPtrIt)];
					if (canBecomeVirtual(attribute, fromPtr, false))
					{
						moveEdgeToPotential(toPtr, *successorPtrIt, attribute, toPtr, fromPtr);
					}
				}
			});
}

ssize_t __get_action_group_priority(const vector< InnerAction >& preActionGroup)
{
	set< string > packageNames;
	ssize_t sum = 0;
	FORIT(actionIt, preActionGroup)
	{
		sum += actionIt->priority;
		packageNames.insert(actionIt->version->packageName);
	}
	return sum / (ssize_t)packageNames.size();
}
struct __action_group_pointer_priority_less
{
	bool operator()(const vector< InnerAction >* left, const vector< InnerAction >* right)
	{
		auto leftPriority = __get_action_group_priority(*left);
		auto rightPriority = __get_action_group_priority(*right);
		if (leftPriority < rightPriority)
		{
			return true;
		}
		else if (leftPriority > rightPriority)
		{
			return false;
		}
		return (*left > *right); // so "lesser" action group have a bigger priority
	}
};

void __set_priority_links(GraphAndAttributes& gaa, bool debugging)
{
	auto adjustPair = [&gaa, &debugging](const InnerAction* fromPtr, const InnerAction* toPtr,
			const InnerAction* unpackActionPtr)
	{
		if (gaa.graph.hasEdge(toPtr, fromPtr))
		{
			return;
		}
		if (debugging)
		{
			debug2("adjusting the pair '%s' -> '%s':", fromPtr->toString(), toPtr->toString());
		}

		std::list< const InnerAction* > notFirstActions = { toPtr };
		if (unpackActionPtr != fromPtr && unpackActionPtr != toPtr)
		{
			notFirstActions.push_back(unpackActionPtr);
		}

		auto reachableFromVertices = gaa.graph.getReachableFrom(*fromPtr);
		FORIT(actionPtrIt, notFirstActions)
		{
			const GraphCessorListType& predecessors = gaa.graph.getPredecessors(*actionPtrIt);
			FORIT(predecessorIt, predecessors)
			{
				if (!reachableFromVertices.count(*predecessorIt))
				{
					// the fact we reached here means:
					// 1) predecessorIt does not belong to a chain being adjusted
					// 2) link 'predecessor' -> 'from' does not create a cycle
					gaa.graph.addEdge(*predecessorIt, fromPtr);
					if (debugging)
					{
						debug2("setting priority link: '%s' -> '%s'",
								(*predecessorIt)->toString(), fromPtr->toString());
					}
				}
			}
		}
	};

	__for_each_package_sequence(gaa.graph, adjustPair);
}

bool __is_single_package_group(const vector< InnerAction >& actionGroup)
{
	const string& firstPackageName = actionGroup[0].version->packageName;
	FORIT(actionIt, actionGroup)
	{
		if (actionIt->version->packageName != firstPackageName)
		{
			return false;
		}
	}
	return true;
}

bool __link_actions(GraphAndAttributes& gaa, bool debugging)
{
	bool linkedSomething = false;

	__set_priority_links(gaa, debugging);
	auto callback = [debugging](const vector< InnerAction >& preActionGroup, bool closing)
	{
		if (debugging)
		{
			vector< string > s;
			FORIT(actionIt, preActionGroup)
			{
				s.push_back(actionIt->toString());
			}
			if (!s.empty())
			{
				auto priority = __get_action_group_priority(preActionGroup);
				debug2("toposort: %s action group: '%s' (priority: %zd)",
						(closing ? "selected" : "opened"), join(", ", s), priority);
			}
		}
	};
	vector< vector< InnerAction > > preActionGroups;
	gaa.graph.topologicalSortOfStronglyConnectedComponents< __action_group_pointer_priority_less >
			(callback, std::back_inserter(preActionGroups));

	auto processCandidates = [&gaa, &debugging, &linkedSomething](const InnerAction& from, const InnerAction& to)
	{
		if (to.linkedFrom)
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
					debug2("new link: '%s' -> '%s'", fromPtr->toString(), toPtr->toString());
				}
				gaa.graph.addEdge(toPtr, fromPtr);
			}
		}
	};

	// contiguous action can be safely linked
	auto preActionGroupsEndIt = preActionGroups.end();
	FORIT(actionGroupIt, preActionGroups)
	{
		FORIT(actionIt, *actionGroupIt)
		{
			const InnerAction& from = *actionIt;
			if (from.linkedTo)
			{
				continue; // was linked already
			}

			if (actionGroupIt->size() != 1)
			{
				// search in the same group
				FORIT(candidateActionIt, *actionGroupIt)
				{
					processCandidates(from, *candidateActionIt);
				}
			}

			auto nextActionGroupIt = actionGroupIt + 1;
			if (nextActionGroupIt != preActionGroupsEndIt)
			{
				bool ll = __is_single_package_group(*actionGroupIt);
				bool rr = __is_single_package_group(*nextActionGroupIt);
				if (from.type == InnerAction::Remove ? (ll || rr) : (ll && rr))
				{
					FORIT(candidateActionIt, *nextActionGroupIt)
					{
						processCandidates(from, *candidateActionIt);
					}
				}
			}
		}
	}

	return linkedSomething;
}

void __iterate_over_graph(const Cache& cache, GraphAndAttributes& gaa,
		bool isMini, bool debugging)
{
	const char* maybeMini = isMini ? "mini " : "";
	do // iterating
	{
		if (debugging)
		{
			debug2("building %saction graph: next iteration", maybeMini);
		}
		__expand_linked_actions(cache, gaa, debugging);
	} while (__link_actions(gaa, debugging));
	if (debugging)
	{
		debug2("building %saction graph: finished", maybeMini);
	}
}

bool PackagesWorker::__build_actions_graph(GraphAndAttributes& gaa)
{
	if (!__desired_state)
	{
		_logger->loggedFatal2(Logger::Subsystem::Packages, 2, format2, "worker: the desired state is not given");
	}

	bool debugging = _config->getBool("debug::worker");

	{
		__fill_actions(gaa);
		// maybe, we have nothing to do?
		if (gaa.graph.getVertices().empty() && __actions_preview->groups[Action::ProcessTriggers].empty())
		{
			return false;
		}

		auto virtualEdges = __create_virtual_actions(gaa, _cache);
		FORIT(it, virtualEdges)
		{
			gaa.graph.addVertex(it->first);
			gaa.graph.addVertex(it->second);
		}
		__for_each_package_sequence(gaa.graph,
				[&gaa](const InnerAction* fromPtr, const InnerAction* toPtr, const InnerAction*)
				{
					// priority edge for shorting distance between package subactions
					gaa.graph.addEdge(toPtr, fromPtr);
				});
		__fill_graph_dependencies(_cache, gaa, debugging);
		__expand_and_delete_virtual_edges(gaa, virtualEdges, debugging);

		__iterate_over_graph(*_cache, gaa, false, debugging);
	}

	if (debugging)
	{
		auto edges = gaa.graph.getEdges();
		FORIT(edgeIt, edges)
		{
			auto attributeLevel = gaa.attributes[make_pair(edgeIt->first, edgeIt->second)].getLevel();
			debug2("the present action dependency: '%s' -> '%s', %s",
					edgeIt->first->toString(), edgeIt->second->toString(),
					GraphAndAttributes::Attribute::levelStrings[attributeLevel]);
		}
	}

	return true;
}

bool __is_circular_action_subgroup_allowed(const vector< InnerAction >& actionSubgroup)
{
	if (__is_single_package_group(actionSubgroup))
	{
		return true;
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
		GraphAndAttributes& miniGaa, set< BinaryVersion::RelationTypes::Type >& removedRelations,
		GraphAndAttributes::Attribute::Level minimumAttributeLevel, bool debugging)
{
	using std::make_pair;

	{ // filling minigraph
		// fill vertices
		FORIT(actionIt, actionGroup)
		{
			auto vertexPtr = miniGaa.graph.addVertex(*actionIt);
			vertexPtr->linkedFrom = NULL;
			vertexPtr->linkedTo = NULL;
		}
		// filling edges
		const set< InnerAction >& allowedVertices = miniGaa.graph.getVertices();
		auto getNewVertex = [&allowedVertices](const InnerAction* oldPtr)
		{
			auto newToIt = allowedVertices.find(*oldPtr);
			return (newToIt != allowedVertices.end()) ? &*newToIt : NULL;
		};
		FORIT(it, allowedVertices)
		{
			auto newFromPtr = &*it;
			auto oldFromPtr = gaa.graph.addVertex(*newFromPtr);

			const GraphCessorListType& oldSuccessors = gaa.graph.getSuccessors(oldFromPtr);
			FORIT(successorPtrIt, oldSuccessors)
			{
				auto oldToPtr = *successorPtrIt;
				auto newToPtr = getNewVertex(oldToPtr);

				if (newToPtr)
				{
					// yes, edge lies inside our mini graph
					const GraphAndAttributes::Attribute& oldAttribute = gaa.attributes[make_pair(oldFromPtr, oldToPtr)];
					auto ignoring = oldAttribute.getLevel() < minimumAttributeLevel;
					if (ignoring)
					{
						FORIT(relationInfoRecordIt, oldAttribute.relationInfo)
						{
							removedRelations.insert(relationInfoRecordIt->dependencyType);
						}
						if (debugging)
						{
							debug2("ignoring edge '%s' -> '%s'", oldFromPtr->toString(), oldToPtr->toString());
						}
					}
					else
					{
						miniGaa.graph.addEdge(newFromPtr, newToPtr);
						miniGaa.attributes[make_pair(newFromPtr, newToPtr)] = oldAttribute;
						if (debugging)
						{
							debug2("adding edge '%s' -> '%s'", newFromPtr->toString(), newToPtr->toString());
						}
					}

					auto edgesToRestoreRange = gaa.potentialEdges.equal_range(make_pair(oldFromPtr, oldToPtr));
					for(auto edgeToRestoreIt = edgesToRestoreRange.first;
							edgeToRestoreIt != edgesToRestoreRange.second; ++edgeToRestoreIt)
					{
						const InnerActionPtrPair& potentialEdgePair = edgeToRestoreIt->second.first;

						auto newPotentialFromPtr = getNewVertex(potentialEdgePair.first);
						if (!newPotentialFromPtr)
						{
							continue;
						}
						auto newPotentialToPtr = getNewVertex(potentialEdgePair.second);
						if (!newPotentialToPtr)
						{
							continue;
						}

						const auto& potentialEdgeAttribute = edgeToRestoreIt->second.second;
						if (potentialEdgeAttribute.getLevel() < minimumAttributeLevel)
						{
							if (debugging)
							{
								debug2("  ignoring potential edge '%s' -> '%s'",
										newPotentialFromPtr->toString(), newPotentialToPtr->toString());
							}
							continue;
						}

						if (ignoring)
						{
							miniGaa.graph.addEdge(newPotentialFromPtr, newPotentialToPtr);
							miniGaa.attributes[make_pair(newPotentialFromPtr, newPotentialToPtr)] = potentialEdgeAttribute;
						}
						else
						{
							miniGaa.potentialEdges.insert(make_pair(
									InnerActionPtrPair(newFromPtr, newToPtr),
									make_pair(InnerActionPtrPair(newPotentialFromPtr, newPotentialToPtr), potentialEdgeAttribute)));
						}
						if (debugging)
						{
							debug2("  %s edge '%s' -> '%s'", (ignoring ? "restoring" : "transferring hidden"),
									newPotentialFromPtr->toString(), newPotentialToPtr->toString());
						}
					}
				}
			}
		}
	}
	__iterate_over_graph(*cache, miniGaa, true, debugging);
}

void __split_heterogeneous_actions(const shared_ptr< const Cache >& cache, Logger& logger,
		vector< InnerActionGroup >& actionGroups, GraphAndAttributes& gaa,
		GraphAndAttributes::Attribute::Level level, bool debugging)
{
	typedef GraphAndAttributes::Attribute Attribute;
	if (debugging)
	{
		debug2("splitting heterogeneous actions, level %s", Attribute::levelStrings[level]);
	}

	auto dummyCallback = [](const vector< InnerAction >&, bool) {};

	vector< InnerActionGroup > newActionGroups;

	FORIT(actionGroupIt, actionGroups)
	{
		const InnerActionGroup& actionGroup = *actionGroupIt;
		if (actionGroup.size() > 1 && !__is_circular_action_subgroup_allowed(actionGroup))
		{
			if (level > Attribute::Hard)
			{
				// no-go
				vector< string > actionStrings;
				FORIT(it, actionGroup)
				{
					actionStrings.push_back(it->toString());
				}
				logger.loggedFatal2(Logger::Subsystem::Packages, 2,
						format2, "internal error: unable to schedule circular actions '%s'", join(", ", actionStrings));
			}

			// we build a mini-graph with reduced number of edges
			GraphAndAttributes miniGaa;
			set< BinaryVersion::RelationTypes::Type > removedRelations;
			__build_mini_action_graph(cache, actionGroup, gaa, miniGaa, removedRelations, level, debugging);

			vector< InnerActionGroup > actionSubgroupsSorted;
			{
				vector< vector< InnerAction > > preActionSubgroups;
				miniGaa.graph.topologicalSortOfStronglyConnectedComponents< __action_group_pointer_priority_less >
						(dummyCallback, std::back_inserter(preActionSubgroups));
				actionSubgroupsSorted = __convert_vector(std::move(preActionSubgroups));
			}
			FORIT(actionSubgroupIt, actionSubgroupsSorted)
			{
				InnerActionGroup& actionSubgroup = *actionSubgroupIt;

				actionSubgroup.dpkgFlags = actionGroup.dpkgFlags;
				if (actionSubgroupsSorted.size() > 1) // self-contained heterogeneous actions don't need it
				{
					FORIT(removedRelationIt, removedRelations)
					{
						switch (*removedRelationIt)
						{
							case BinaryVersion::RelationTypes::Depends:
							case BinaryVersion::RelationTypes::PreDepends:
								actionSubgroup.dpkgFlags.insert("--force-depends");
								break;
							case BinaryVersion::RelationTypes::Breaks:
								actionSubgroup.dpkgFlags.insert("--force-breaks");
								break;
							default:
								logger.loggedFatal2(Logger::Subsystem::Packages, 2,
										format2, "internal error: worker: a relation '%s' cannot be soft",
										BinaryVersion::RelationTypes::rawStrings[*removedRelationIt]);
						}
					}
				}
				if (level - 1 > Attribute::Priority) // level - 1 == highest level of removed edges
				{
					actionSubgroup.continued = true;
				}

			}
			if (!actionGroup.continued)
			{
				actionSubgroupsSorted.rbegin()->continued = false;
			}

			__split_heterogeneous_actions(cache, logger, actionSubgroupsSorted, miniGaa,
					Attribute::Level((int)level+1), debugging);

			FORIT(actionSubgroupIt, actionSubgroupsSorted)
			{
				newActionGroups.push_back(*actionSubgroupIt);
			}
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
			debug2("split action group: %s", join(", ", strings));
		}
	}

	newActionGroups.swap(actionGroups);
}

static string __get_long_alias_tail(const Version& version, const string& baseUri)
{
	return format2("%s %s %s", version.getCodenameAndComponentString(baseUri),
			version.packageName, version.versionString);
}

map< string, pair< download::Manager::DownloadEntity, string > > PackagesWorker::__prepare_downloads()
{
	_logger->log(Logger::Subsystem::Packages, 2, "preparing downloads");

	map< string, pair< download::Manager::DownloadEntity, string > > downloads;
	try
	{
		auto archivesDirectory = _get_archives_directory();

		if (!_config->getBool("cupt::worker::simulate"))
		{
			try
			{
				fs::mkpath(archivesDirectory);
				string partialDirectory = archivesDirectory + partialDirectorySuffix;
				fs::mkpath(partialDirectory);
			}
			catch (...)
			{
				_logger->loggedFatal2(Logger::Subsystem::Packages, 3,
						format2, "unable to create the archive downloads directory");
			}
		}


		DebdeltaHelper debdeltaHelper(*_config);

		for (auto actionType: _download_dependent_action_types)
		{
			const auto& suggestedPackages = __actions_preview->groups[actionType];
			FORIT(it, suggestedPackages)
			{
				const auto& version = it->second.version;

				const string& packageName = version->packageName;
				const string& versionString = version->versionString;

				auto downloadInfo = version->getDownloadInfo();

				// we need at least one real uri
				if (downloadInfo.empty())
				{
					_logger->loggedFatal2(Logger::Subsystem::Packages, 3,
							format2, "no available download URIs for %s %s", packageName, versionString);
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

				FORIT(it, downloadInfo)
				{
					string uri = it->baseUri + '/' + it->directory + '/' + version->file.name;

					string shortAlias = packageName;
					string longAlias = it->baseUri + ' ' + __get_long_alias_tail(*version, it->baseUri);

					downloadEntity.extendedUris.push_back(
							download::Manager::ExtendedUri(uri, shortAlias, longAlias));
				}
				{
					auto debdeltaDownloadInfo = debdeltaHelper.getDownloadInfo(version, _cache);
					FORIT(it, debdeltaDownloadInfo)
					{
						const string& uri = it->uri;
						string longAlias = it->baseUri + ' ' + __get_long_alias_tail(*version, it->baseUri);

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
						return __("unable to find the downloaded file");
					}
					if (!version->file.hashSums.verify(downloadPath))
					{
						unlink(downloadPath.c_str()); // intentionally ignore errors if any
						return __("hash sums mismatch");
					}
					if (!fs::move(downloadPath, targetPath))
					{
						return format2e(__("unable to rename '%s' to '%s'"), downloadPath, targetPath);
					}
					return string();
				};

				auto downloadValue = std::make_pair(std::move(downloadEntity), targetPath);
				downloads.insert(std::make_pair(packageName, downloadValue));
			}
		}
	}
	catch (...)
	{
		_logger->loggedFatal2(Logger::Subsystem::Packages, 2, format2, "failed to prepare downloads");
	}

	return downloads;
}

vector< Changeset > __split_action_groups_into_changesets(Logger& logger,
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
		logger.loggedFatal2(Logger::Subsystem::Packages, 2,
				format2, "internal error: packages have been left unconfigured: '%s'", join(" ", unconfiguredPackageNames));
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
		debug2("the changeset download amounts: maximum: %s, all: %s",
				humanReadableSizeString(result), join(", ", amounts));
	}

	return result;
}

void __set_force_options_for_removals_if_needed(const Cache& cache,
		Logger& logger, vector< InnerActionGroup >& actionGroups)
{
	auto systemState = cache.getSystemState();
	FORIT(actionGroupIt, actionGroups)
	{
		bool removeReinstreqFlagIsSet = false;
		bool removeEssentialFlagIsSet = false;

		FORIT(actionIt, *actionGroupIt)
		{
			if (actionIt->type == InnerAction::Remove)
			{
				const string& packageName = actionIt->version->packageName;
				auto nextActionIt = actionIt+1;
				if (nextActionIt != actionGroupIt->end())
				{
					if (nextActionIt->type == InnerAction::Unpack &&
							nextActionIt->version->packageName == packageName)
					{
						continue; // okay, this is not really a removal, we can ignore it
					}
				}

				if (!removeReinstreqFlagIsSet)
				{
					auto installedRecord = systemState->getInstalledInfo(packageName);
					if (!installedRecord)
					{
						logger.loggedFatal2(Logger::Subsystem::Packages, 2,
								format2, "internal error: worker: __set_force_options_for_removals_if_needed: "
								"there is no installed record for the package '%s' which is to be removed",
								packageName);
					}
					typedef system::State::InstalledRecord::Flag IRFlag;
					if (installedRecord->flag == IRFlag::Reinstreq)
					{
						actionGroupIt->dpkgFlags.insert("--force-remove-reinstreq");
						removeReinstreqFlagIsSet = true;
					}
				}

				if (!removeEssentialFlagIsSet)
				{
					if (actionIt->version->essential)
					{
						actionGroupIt->dpkgFlags.insert("--force-remove-essential");
						removeEssentialFlagIsSet = true;
					}
				}
			}
		}
	}
}

void __get_action_groups(const shared_ptr< const Cache >& cache, Logger& logger,
		GraphAndAttributes& gaa, vector< InnerActionGroup >* actionGroupsPtr,
		bool debugging)
{
	auto dummyCallback = [](const vector< InnerAction >&, bool) {};
	vector< vector< InnerAction > > preActionGroups;
	gaa.graph.topologicalSortOfStronglyConnectedComponents< __action_group_pointer_priority_less >
			(dummyCallback, std::back_inserter(preActionGroups));
	*actionGroupsPtr = __convert_vector(std::move(preActionGroups));

	typedef GraphAndAttributes::Attribute Attribute;
	auto initialSplitLevel = Attribute::FromVirtual;
	__split_heterogeneous_actions(cache, logger, *actionGroupsPtr, gaa, initialSplitLevel, debugging);

	__set_force_options_for_removals_if_needed(*cache, logger, *actionGroupsPtr);
}

vector< Changeset > PackagesWorker::__get_changesets(GraphAndAttributes& gaa,
		const map< string, pair< download::Manager::DownloadEntity, string > >& downloads)
{
	auto debugging = _config->getBool("debug::worker");
	size_t archivesSpaceLimit = _config->getInteger("cupt::worker::archives-space-limit");

	vector< Changeset > changesets;
	{
		vector< InnerActionGroup > actionGroups;
		__get_action_groups(_cache, *_logger, gaa, &actionGroups, debugging);
		changesets = __split_action_groups_into_changesets(*_logger, actionGroups, downloads);
	}

	if (archivesSpaceLimit)
	{
		auto maxDownloadAmount = __get_max_download_amount(changesets, debugging);
		if (debugging)
		{
			debug2("the changeset download amounts: maximum: %s",
					humanReadableSizeString(maxDownloadAmount));
		}
		if (maxDownloadAmount > archivesSpaceLimit)
		{
			// we failed to fit in limit
			_logger->loggedFatal2(Logger::Subsystem::Packages, 3,
					format2, "unable to fit in the archives space limit '%zu', best try is '%zu'",
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

void PackagesWorker::__run_dpkg_command(const string& flavor, const string& command, const CommandInput& commandInput)
{
	auto errorId = format2(__("dpkg '%s' action '%s'"), flavor, command);
	_run_external_command(Logger::Subsystem::Packages, command, commandInput, errorId);
}

void PackagesWorker::__clean_downloads(const Changeset& changeset)
{
	_logger->log(Logger::Subsystem::Packages, 2, "cleaning downloaded archives");
	try
	{
		internal::Lock archivesLock(*_config, _get_archives_directory() + "/lock");

		bool simulating = _config->getBool("cupt::worker::simulate");
		FORIT(it, changeset.downloads)
		{
			const string& targetPath = it->second;
			if (simulating)
			{
				simulate2("removing archive '%s'", targetPath);
			}
			else
			{
				if (unlink(targetPath.c_str()) == -1)
				{
					_logger->loggedFatal2(Logger::Subsystem::Packages, 3,
							format2e, "unable to remove the file '%s'", targetPath);
				}
			}
		}
	}
	catch (...)
	{
		_logger->loggedFatal2(Logger::Subsystem::Packages, 2, format2, "failed to clean downloaded archives");
	}
}

void PackagesWorker::__do_dpkg_pre_actions()
{
	_logger->log(Logger::Subsystem::Packages, 2, "running dpkg pre-invoke hooks");
	auto commands = _config->getList("dpkg::pre-invoke");
	FORIT(commandIt, commands)
	{
		__run_dpkg_command("pre", *commandIt, {});
	}
}

string PackagesWorker::p_generateInputForPreinstallV1Hooks(const vector<InnerActionGroup>& actionGroups)
{
	string result;

	auto archivesDirectory = _get_archives_directory();
	// new debs are pulled to command through STDIN, one by line
	for (const auto& actionGroup: actionGroups)
	{
		for (const auto& action: actionGroup)
		{
			if (action.type == InnerAction::Unpack)
			{
				auto debPath = archivesDirectory + "/" + _get_archive_basename(action.version);
				result += debPath;
				result += "\n";
			}
		}
	}

	return result;
}

static string writeOutConfiguration(const Config& config)
{
	string result;

	auto printKeyValue = [&result](const string& key, const string& value)
	{
		if (!value.empty())
		{
			result += (key + "=" + value + "\n");
		}
	};

	// apt-listbugs wants it case-sensitively
	printKeyValue("APT::Architecture", config.getString("apt::architecture"));

	for (const string& key: config.getScalarOptionNames())
	{
		printKeyValue(key, config.getString(key));
	}
	for (const string& key: config.getListOptionNames())
	{
		for (const string& value: config.getList(key))
		{
			printKeyValue(key + "::", value);
		}
	}
	result += "\n";

	return result;
}

static inline string getActionForPreinstallPackagesHook(InnerAction::Type actionType)
{
	switch (actionType)
	{
		case InnerAction::Configure:
			return "**CONFIGURE**";
		case InnerAction::Remove:
			return "**REMOVE**";
		case InnerAction::Unpack:
			return string();
		default:
			fatal2i("worker: packages: getActionForPreinstallPackagesHook: invalid actionType");
			return string();
	}
}

static string getCompareVersionStringsSignForPreinstallPackagesHook(
		const string& oldVersionString, const string& newVersionString)
{
	if (oldVersionString == "-")
	{
		return "<";
	}
	else if (newVersionString == "-")
	{
		return ">";
	}
	else
	{
		auto comparisonResult = compareVersionStrings(oldVersionString, newVersionString);
		if (comparisonResult < 0)
		{
			return "<";
		}
		else if (comparisonResult == 0)
		{
			return "=";
		}
		else
		{
			return ">";
		}
	}
}

static string getOldVersionString(const BinaryPackage* oldPackage)
{
	string result = "-";

	if (oldPackage)
	{
		if (auto installedVersion = oldPackage->getInstalledVersion())
		{
			result = getOriginalVersionString(installedVersion->versionString).toStdString();
		}
	}

	return result;
}

static string getNewVersionString(const BinaryVersion* version)
{
	return getOriginalVersionString(version->versionString).toStdString();
}

string PackagesWorker::p_generateInputForPreinstallV2OrV3Hooks(
		const vector<InnerActionGroup>& actionGroups, bool v3)
{
	// all hate undocumented formats...
	string result = format2("VERSION %zu\n", size_t(v3 ? 3 : 2));

	result += writeOutConfiguration(*_config);

	auto writeOutVersionString = [v3](const string& versionString, const BinaryVersion* version) -> string
	{
		if (v3)
		{
			return format2("%s %s -", versionString, version->architecture);
		}
		else
		{
			return versionString;
		}
	};

	auto archivesDirectory = _get_archives_directory();
	for (const auto& actionGroup: actionGroups)
	{
		for (const auto& action: actionGroup)
		{
			const auto& version = action.version;

			string path = getActionForPreinstallPackagesHook(action.type);
			if (path.empty())
			{
				path = archivesDirectory + "/" + _get_archive_basename(version);
			}

			const string& packageName = version->packageName;

			string oldVersionString = getOldVersionString(_cache->getBinaryPackage(packageName));
			string newVersionString = (action.type == InnerAction::Remove ? "-" : getNewVersionString(version));

			result += format2("%s %s %s %s %s\n",
					packageName,
					writeOutVersionString(oldVersionString, version),
					getCompareVersionStringsSignForPreinstallPackagesHook(oldVersionString, newVersionString),
					writeOutVersionString(newVersionString, version),
					path);
		}
	}

	// strip last "\n", because apt-listchanges cannot live with it somewhy
	result.erase(result.end() - 1);

	return result;
}

static string getCommandBinaryForPreInstallPackagesHook(const string& command)
{
	string commandBinary = command;

	auto spaceOffset = commandBinary.find(' ');
	if (spaceOffset != string::npos)
	{
		commandBinary.resize(spaceOffset);
	}

	return commandBinary;
}

void PackagesWorker::__do_dpkg_pre_packages_actions(const vector< InnerActionGroup >& actionGroups)
{
	_logger->log(Logger::Subsystem::Packages, 2, "running dpkg pre-install-packages hooks");

	for (const string& command: _config->getList("dpkg::pre-install-pkgs"))
	{
		auto commandInput = p_getCommandInputForPreinstallPackagesHook(command, actionGroups);
		if (commandInput.buffer.empty()) continue;
		setenv("APT_HOOK_INFO_FD", std::to_string(commandInput.fd).c_str(), 1);

		__run_dpkg_command("pre", command, commandInput);
	}
}

CommandInput PackagesWorker::p_getCommandInputForPreinstallPackagesHook(
		const string& command, const vector<InnerActionGroup>& actionGroups)
{
	string commandBinary = getCommandBinaryForPreInstallPackagesHook(command);

	auto hookOptionNamePrefix = string("dpkg::tools::options::") + commandBinary;
	auto versionOfInput = _config->getInteger(hookOptionNamePrefix+"::version");

	if (versionOfInput == 2 || versionOfInput == 3)
	{
		CommandInput ci = p_generateInputForPreinstallV2OrV3Hooks(actionGroups, versionOfInput==3);
		ci.fd = _config->getInteger(hookOptionNamePrefix+"::infofd");
		return ci;
	}
	else
	{
		return p_generateInputForPreinstallV1Hooks(actionGroups);
	}
}

void PackagesWorker::__do_dpkg_post_actions()
{
	_logger->log(Logger::Subsystem::Packages, 2, "running dpkg post-invoke hooks");

	auto commands = _config->getList("dpkg::post-invoke");
	FORIT(commandIt, commands)
	{
		__run_dpkg_command("post", *commandIt, {});
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
			p_markAsAutomaticallyInstalled(packageName, targetStatus);
		}
	}
}

void PackagesWorker::p_markAsAutomaticallyInstalled(const string& packageName, bool targetStatus)
{
	auto simulating = _config->getBool("cupt::worker::simulate");

	{ // logging
		auto message = format2("marking '%s' as %s installed",
				packageName, targetStatus ? "automatically" : "manually");
		_logger->log(Logger::Subsystem::Packages, 2, message);
	}

	if (simulating)
	{
		string prefix = targetStatus ?
					__("marking as automatically installed") : __("marking as manually installed");
		simulate2("%s: %s", prefix, packageName);
	}
	else
	{
		try
		{
			static const string autoFlagField = "Auto-Installed";
			auto& record = p_actualExtendedInfo[packageName];
			if (targetStatus)
			{
				record[autoFlagField] = "1";
			}
			else
			{
				record.erase(autoFlagField);
			}

			p_writeExtendedStateFile();
		}
		catch (...)
		{
			_logger->loggedFatal2(Logger::Subsystem::Packages, 2,
					format2, "failed to change the 'automatically installed' flag");
		}
	}
}

static void fillExtendedStatesFile(File& file, const Cache::ExtendedInfo::Raw& input)
{
	for (const auto& packageRecord: input)
	{
		const auto& packageName = packageRecord.first;
		const auto& fieldData = packageRecord.second;

		if (!fieldData.empty())
		{
			file.put(format2("Package: %s\n", packageName));
			for (const auto& fieldRecord: fieldData)
			{
				file.put(format2("%s: %s\n", fieldRecord.first, fieldRecord.second));
			}
			file.put("\n");
		}
	}
}

void PackagesWorker::p_writeExtendedStateFile()
{
	auto extendedInfoPath = cachefiles::getPathOfExtendedStates(*_config);
	fs::mkpath(fs::dirname(extendedInfoPath));
	auto tempPath = extendedInfoPath + ".cupt.tmp";

	{
		string errorString;
		File tempFile(tempPath, "w", errorString);
		if (!errorString.empty())
		{
			_logger->loggedFatal2(Logger::Subsystem::Packages, 3,
					format2, "unable to open the file '%s': %s", tempPath, errorString);
		}

		fillExtendedStatesFile(tempFile, p_actualExtendedInfo);
	}

	if (!fs::move(tempPath, extendedInfoPath))
	{
		_logger->loggedFatal2(Logger::Subsystem::Packages, 3,
				format2e, "unable to renew extended states file: unable to rename '%s' to '%s'",
				tempPath, extendedInfoPath);
	}
}

void PackagesWorker::__do_downloads(const vector< pair< download::Manager::DownloadEntity, string > >& downloads,
		const shared_ptr< download::Progress >& downloadProgress)
{
	// don't bother ourselves with download preparings if nothing to download
	if (!downloads.empty())
	{
		_logger->log(Logger::Subsystem::Packages, 2, "downloading packages");
		auto archivesDirectory = _get_archives_directory();

		string downloadResult;
		{
			Lock lock(*_config, archivesDirectory + "/lock");

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
			_logger->loggedFatal2(Logger::Subsystem::Packages, 2, format2, "there were download errors");
		}
	}
}

void PackagesWorker::__do_independent_auto_status_changes()
{
	auto wasDoneAlready = [this](const string& packageName)
	{
		for (const auto& actionGroup: __actions_preview->groups)
		{
			if (actionGroup.count(packageName))
			{
				return true;
			}
		}
		return false;
	};

	for (const auto& autoFlagChange: __actions_preview->autoFlagChanges)
	{
		const auto& packageName = autoFlagChange.first;
		if (!wasDoneAlready(packageName))
		{
			p_markAsAutomaticallyInstalled(packageName, autoFlagChange.second);
		}
	}
}

void PackagesWorker::p_processActionGroup(Dpkg& dpkg, const InnerActionGroup& actionGroup)
{
	if (actionGroup.getCompoundActionType() != InnerAction::Remove)
	{
		__change_auto_status(actionGroup);
	}
	dpkg.doActionGroup(actionGroup, *__actions_preview);
	if (actionGroup.getCompoundActionType() == InnerAction::Remove)
	{
		__change_auto_status(actionGroup);
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

	_logger->log(Logger::Subsystem::Packages, 1, "scheduling dpkg actions");

	vector< Changeset > changesets;
	{
		GraphAndAttributes gaa;
		if (!__build_actions_graph(gaa) && __actions_preview->autoFlagChanges.empty())
		{
			_logger->log(Logger::Subsystem::Packages, 1, "nothing to do");
			return;
		}

		_logger->log(Logger::Subsystem::Packages, 2, "computing dpkg action sequence");
		changesets = __get_changesets(gaa, preDownloads);
	}

	_logger->log(Logger::Subsystem::Packages, 1, "changing the system");

	__do_dpkg_pre_actions();
	{
		for (const Changeset& changeset: changesets)
		{
			Dpkg dpkg(this);

			if (debugging) debug2("started changeset");
			__do_downloads(changeset.downloads, downloadProgress);
			__do_dpkg_pre_packages_actions(changeset.actionGroups);
			for (const auto& actionGroup: changeset.actionGroups)
			{
				p_processActionGroup(dpkg, actionGroup);
			}
			if (archivesSpaceLimit) __clean_downloads(changeset);
			if (debugging) debug2("finished changeset");
		}
		__do_independent_auto_status_changes();
	}
	__do_dpkg_post_actions();
}

}
}

