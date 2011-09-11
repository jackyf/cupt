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
#include <cupt/system/state.hpp>
#include <cupt/file.hpp>
#include <cupt/download/progress.hpp>

#include <internal/filesystem.hpp>
#include <internal/debdeltahelper.hpp>
#include <internal/lock.hpp>

#include <internal/worker/packages.hpp>

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
		{ Action::Configure, vector< IA::Type >{ IA::Configure } },
		{ Action::Deconfigure, vector< IA::Type >{ IA::Remove } },
		{ Action::Remove, vector< IA::Type >{ IA::Remove } },
		{ Action::Purge, vector< IA::Type >{ IA::Remove } },
	};

	auto pseudoEssentialPackageNames = __get_pseudo_essential_package_names(
			*_cache, _config->getBool("debug::worker"));

	auto addBasicEdge = [&gaa](const InnerAction* fromPtr, const InnerAction* toPtr)
	{
		gaa.graph.addEdgeFromPointers(fromPtr, toPtr);
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
					versionPtr->versionString = "<dummy>";
					versionPtr->essential = false;
					version.reset(versionPtr);
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

			gi.gaaPtr->graph.addEdgeFromPointers(slaveActionPtr, masterActionPtr);

			bool fromVirtual = slaveActionPtr->fake || masterActionPtr->fake;
			// adding relation to attributes
			vector< GraphAndAttributes::RelationInfoRecord >& relationInfo =
					gi.gaaPtr->attributes[make_pair(slaveActionPtr, masterActionPtr)].relationInfo;
			GraphAndAttributes::RelationInfoRecord record =
					{ dependencyType, *relationExpressionIt, direction == Direction::After, fromVirtual };
			relationInfo.push_back(std::move(record));

			if (gi.debugging)
			{
				debug("new action dependency: '%s' -> '%s', reason: '%s: %s'", slaveActionPtr->toString().c_str(),
						masterActionPtr->toString().c_str(), BinaryVersion::RelationTypes::rawStrings[dependencyType],
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

const shared_ptr< const BinaryVersion > __create_virtual_version(
		const shared_ptr< const BinaryVersion >& version)
{
	typedef BinaryVersion::RelationTypes RT;

	shared_ptr< BinaryVersion > virtualVersion(new BinaryVersion);
	virtualVersion->packageName = version->packageName;
	virtualVersion->versionString = version->versionString;
	virtualVersion->relations[RT::PreDepends] = version->relations[RT::PreDepends];
	virtualVersion->relations[RT::Depends] = version->relations[RT::Depends];
	virtualVersion->essential = false;
	return virtualVersion;
}

void __create_virtual_edge(
		const shared_ptr< const BinaryVersion >& fromVirtualVersion,
		const shared_ptr< const BinaryVersion >& toVirtualVersion,
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

		auto virtualVersion = __create_virtual_version(installedVersion);
		__create_virtual_edge(virtualVersion, virtualVersion, &virtualEdges);
	}

	return virtualEdges;
}

bool __share_relation_expression(const GraphAndAttributes::Attribute& left,
		const GraphAndAttributes::Attribute& right)
{
	FORIT(leftRelationRecordIt, left.relationInfo)
	{
		FORIT(rightRelationRecordIt, right.relationInfo)
		{
			if (leftRelationRecordIt->dependencyType == rightRelationRecordIt->dependencyType &&
					leftRelationRecordIt->relationExpression == rightRelationRecordIt->relationExpression)
			{
				return true;
			}
		}
	}
	return false;
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

			const GraphCessorListType& predecessors = graph.getPredecessorsFromPointer(&*innerActionIt);
			FORIT(actionPtrIt, predecessors)
			{
				if ((*actionPtrIt)->type == InnerAction::Remove &&
					(*actionPtrIt)->version->packageName == packageName)
				{
					fromPtr = *actionPtrIt;
					break;
				}
			}

			const GraphCessorListType& successors = graph.getSuccessorsFromPointer(&*innerActionIt);
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
		debug("moving edge '%s' -> '%s' to edge '%s' -> '%s'",
				fromPredecessorPtr->toString().c_str(), fromSuccessorPtr->toString().c_str(),
				toPredecessorPtr->toString().c_str(), toSuccessorPtr->toString().c_str());
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

	gaa.graph.addEdgeFromPointers(toPredecessorPtr, toSuccessorPtr);
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
		const GraphCessorListType predecessors = gaa.graph.getPredecessorsFromPointer(fromPtr);
		const GraphCessorListType successors = gaa.graph.getSuccessorsFromPointer(toPtr);
		FORIT(predecessorVertexPtrIt, predecessors)
		{
			FORIT(successorVertexPtrIt, successors)
			{
				if (*predecessorVertexPtrIt == *successorVertexPtrIt)
				{
					continue;
				}
				if (!__share_relation_expression(
							gaa.attributes[make_pair(*predecessorVertexPtrIt, fromPtr)],
							gaa.attributes[make_pair(toPtr, *successorVertexPtrIt)]))
				{
					continue;
				}

				__move_edge(gaa, *predecessorVertexPtrIt, fromPtr, *predecessorVertexPtrIt, *successorVertexPtrIt, debugging);
				__move_edge(gaa, toPtr, *successorVertexPtrIt, *predecessorVertexPtrIt, *successorVertexPtrIt, debugging);
				if (debugging)
				{
					const string& mediatorPackageName = fromPtr->version->packageName;
					debug("multiplied action dependency: '%s' -> '%s', virtual mediator: '%s'",
							(*predecessorVertexPtrIt)->toString().c_str(), (*successorVertexPtrIt)->toString().c_str(),
							mediatorPackageName.c_str());
				}

			}
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
				auto predicate = std::bind2nd(PointerEqual< const BinaryVersion >(), antagonisticPtr->version);
				if (std::find_if(satisfyingVersions.begin(), satisfyingVersions.end(),
						predicate) == satisfyingVersions.end())
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

		gaa.graph.deleteEdgeFromPointers(fromPredecessorPtr, fromSuccessorPtr);
		if (debugging)
		{
			debug("deleting the edge '%s' -> '%s'",
					fromPredecessorPtr->toString().c_str(), fromSuccessorPtr->toString().c_str());
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

				const GraphCessorListType predecessors = gaa.graph.getPredecessorsFromPointer(fromPtr); // copying
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

				const GraphCessorListType successors = gaa.graph.getSuccessorsFromPointer(toPtr);
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
		if (gaa.graph.hasEdgeFromPointers(toPtr, fromPtr))
		{
			return;
		}
		if (debugging)
		{
			debug("adjusting the pair '%s' -> '%s':",
					fromPtr->toString().c_str(), toPtr->toString().c_str());
		}

		std::list< const InnerAction* > notFirstActions = { toPtr };
		if (unpackActionPtr != fromPtr && unpackActionPtr != toPtr)
		{
			notFirstActions.push_back(unpackActionPtr);
		}

		auto reachableFromVertices = gaa.graph.getReachableFrom(*fromPtr);
		FORIT(actionPtrIt, notFirstActions)
		{
			const GraphCessorListType& predecessors = gaa.graph.getPredecessorsFromPointer(*actionPtrIt);
			FORIT(predecessorIt, predecessors)
			{
				if (!reachableFromVertices.count(*predecessorIt))
				{
					// the fact we reached here means:
					// 1) predecessorIt does not belong to a chain being adjusted
					// 2) link 'predecessor' -> 'from' does not create a cycle
					gaa.graph.addEdgeFromPointers(*predecessorIt, fromPtr);
					if (debugging)
					{
						debug("setting priority link: '%s' -> '%s'",
								(*predecessorIt)->toString().c_str(), fromPtr->toString().c_str());
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
				debug("toposort: %s action group: '%s' (priority: %zd)",
						(closing ? "selected" : "opened"), join(", ", s).c_str(), priority);
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
					debug("new link: '%s' -> '%s'",
							fromPtr->toString().c_str(), toPtr->toString().c_str());
				}
				gaa.graph.addEdgeFromPointers(toPtr, fromPtr);
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
			debug("building %saction graph: next iteration", maybeMini);
		}
		__expand_linked_actions(cache, gaa, debugging);
	} while (__link_actions(gaa, debugging));
	if (debugging)
	{
		debug("building %saction graph: finished", maybeMini);
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
					gaa.graph.addEdgeFromPointers(toPtr, fromPtr);
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
			debug("the present action dependency: '%s' -> '%s', %s",
					edgeIt->first->toString().c_str(), edgeIt->second->toString().c_str(),
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

			const GraphCessorListType& oldSuccessors = gaa.graph.getSuccessorsFromPointer(oldFromPtr);
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
							debug("ignoring edge '%s' -> '%s'",
									oldFromPtr->toString().c_str(), oldToPtr->toString().c_str());
						}
					}
					else
					{
						miniGaa.graph.addEdgeFromPointers(newFromPtr, newToPtr);
						miniGaa.attributes[make_pair(newFromPtr, newToPtr)] = oldAttribute;
						if (debugging)
						{
							debug("adding edge '%s' -> '%s'",
									newFromPtr->toString().c_str(), newToPtr->toString().c_str());
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

						if (ignoring)
						{
							miniGaa.graph.addEdgeFromPointers(newPotentialFromPtr, newPotentialToPtr);
							miniGaa.attributes[make_pair(newPotentialFromPtr, newPotentialToPtr)] =
									edgeToRestoreIt->second.second;
						}
						else
						{
							miniGaa.potentialEdges.insert(make_pair(
									InnerActionPtrPair(newFromPtr, newToPtr),
									make_pair(InnerActionPtrPair(newPotentialFromPtr, newPotentialToPtr), edgeToRestoreIt->second.second)));
						}
						if (debugging)
						{
							debug("  %s edge '%s' -> '%s'", (ignoring ? "restoring" : "transferring hidden"),
									newPotentialFromPtr->toString().c_str(), newPotentialToPtr->toString().c_str());
						}
					}
				}
			}
		}
	}
	__iterate_over_graph(*cache, miniGaa, true, debugging);
}

void __split_heterogeneous_actions(const shared_ptr< const Cache >& cache,
		vector< InnerActionGroup >& actionGroups, GraphAndAttributes& gaa,
		GraphAndAttributes::Attribute::Level level, bool debugging)
{
	typedef GraphAndAttributes::Attribute Attribute;
	if (debugging)
	{
		debug("splitting heterogeneous actions, level %s", Attribute::levelStrings[level]);
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
				fatal("internal error: unable to schedule circular actions '%s'", join(", ", actionStrings).c_str());
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
								fatal("internal error: worker: a relation '%s' cannot be soft",
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

			__split_heterogeneous_actions(cache, actionSubgroupsSorted, miniGaa,
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

void __set_force_options_for_removals_if_needed(const Cache& cache,
		vector< InnerActionGroup >& actionGroups)
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
						fatal("internal error: worker: __set_force_options_for_removals_if_needed: "
								"there is no installed record for the package '%s' which is to be removed",
								packageName.c_str());
					}
					typedef system::State::InstalledRecord::Flag IRFlag;
					if (installedRecord->flag == IRFlag::Reinstreq || installedRecord->flag == IRFlag::HoldAndReinstreq)
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

void __get_action_groups(const shared_ptr< const Cache >& cache,
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
	__split_heterogeneous_actions(cache, *actionGroupsPtr, gaa, initialSplitLevel, debugging);

	__set_force_options_for_removals_if_needed(*cache, *actionGroupsPtr);
}

vector< Changeset > PackagesWorker::__get_changesets(GraphAndAttributes& gaa,
		const map< string, pair< download::Manager::DownloadEntity, string > >& downloads)
{
	auto debugging = _config->getBool("debug::worker");
	size_t archivesSpaceLimit = _config->getInteger("cupt::worker::archives-space-limit");

	vector< Changeset > changesets;
	{
		vector< InnerActionGroup > actionGroups;
		__get_action_groups(_cache, gaa, &actionGroups, debugging);
		changesets = __split_action_groups_into_changesets(actionGroups, downloads);
	}

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

void PackagesWorker::__run_dpkg_command(const string& flavor, const string& command, const string& commandInput)
{
	auto errorId = sf(__("dpkg '%s' action '%s'"), flavor.c_str(), command.c_str());
	_run_external_command(Logger::Subsystem::Packages, command, commandInput, errorId);
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
	_logger->log(Logger::Subsystem::Packages, 2, "running dpkg pre-invoke hooks");
	auto commands = _config->getList("dpkg::pre-invoke");
	FORIT(commandIt, commands)
	{
		__run_dpkg_command("pre", *commandIt, "");
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
				if (comparisonResult < 0)
				{
					compareVersionStringsSign = "<";
				}
				else if (comparisonResult == 0)
				{
					compareVersionStringsSign = "=";
				}
				else
				{
					compareVersionStringsSign = ">";
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
	_logger->log(Logger::Subsystem::Packages, 2, "running dpkg pre-install-packages hooks");

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
			if (commandInput.empty())
			{
				continue;
			}
		}

		__run_dpkg_command("pre", command, commandInput);
	}
}

void PackagesWorker::__do_dpkg_post_actions()
{
	_logger->log(Logger::Subsystem::Packages, 2, "running dpkg post-invoke hooks");

	auto commands = _config->getList("dpkg::post-invoke");
	FORIT(commandIt, commands)
	{
		__run_dpkg_command("post", *commandIt, "");
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

	{ // logging
		auto message = sf("marking '%s' as %s installed",
				packageName.c_str(), targetStatus ? "automatically" : "manually");
		_logger->log(Logger::Subsystem::Packages, 2, message);
	}

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
		_logger->log(Logger::Subsystem::Packages, 2, "downloading packages");
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

string __get_dpkg_action_log(const InnerActionGroup& actionGroup,
		InnerAction::Type actionType, const string& actionName)
{
	vector< string > subResults;
	FORIT(actionIt, actionGroup)
	{
		if (actionIt->type == actionType)
		{
			subResults.push_back(actionName + ' ' + actionIt->version->packageName
					+ ' ' + actionIt->version->versionString);
		}
	}
	return join(" & ", subResults);
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
		if (!__build_actions_graph(gaa))
		{
			_logger->log(Logger::Subsystem::Packages, 1, "nothing to do");
			return;
		}

		_logger->log(Logger::Subsystem::Packages, 2, "computing dpkg action sequence");
		changesets = __get_changesets(gaa, preDownloads);
	}

	_logger->log(Logger::Subsystem::Packages, 1, "changing the system");

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
		_logger->log(Logger::Subsystem::Packages, 2, "running all package triggers");

		auto command = dpkgBinary + " --triggers-only -a";
		__run_dpkg_command("triggers-only", command, "");
	}

	auto archivesDirectory = _get_archives_directory();
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
				case InnerAction::Remove:
				{
					const string& packageName = actionGroupIt->rbegin()->version->packageName;
					actionName = __actions_preview->groups[Action::Purge].count(packageName) ?
							"purge" : "remove";
				}
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
				string requestedDpkgOptions;
				FORIT(dpkgFlagIt, actionGroupIt->dpkgFlags)
				{
					requestedDpkgOptions += string(" ") + *dpkgFlagIt;
				}
				dpkgCommand += requestedDpkgOptions;

				/* the workaround for a dpkg bug #558151

				   dpkg performs some IMHO useless checks for programs in PATH
				   which breaks some upgrades of packages that contains important programs

				   It is possible that this hack is not needed anymore with
				   better scheduling heuristics of 2.x but we cannot
				   re-evaluate it with lenny->squeeze (e)glibc upgrade since
				   new Cupt requires new libgcc which in turn requires new
				   glibc.
				*/
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
				{ // debug & logs
					if (debugging)
					{
						vector< string > stringifiedActions;
						FORIT(actionIt, *actionGroupIt)
						{
							stringifiedActions.push_back(actionIt->toString());
						}
						auto actionsString = join(" & ", stringifiedActions);
						debug("do: (%s) %s%s", actionsString.c_str(), requestedDpkgOptions.c_str(),
								actionGroupIt->continued ? " (continued)" : "");
					}
					_logger->log(Logger::Subsystem::Packages, 2,
							__get_dpkg_action_log(*actionGroupIt, actionType, actionName));
				}
				_run_external_command(Logger::Subsystem::Packages, dpkgCommand);
			};
		}
		if (deferTriggers)
		{
			// triggers were not processed during actions perfomed before, do it now at once
			_logger->log(Logger::Subsystem::Packages, 2, "running pending triggers");

			string command = dpkgBinary + " --triggers-only --pending";
			if (debugging)
			{
				debug("running triggers");
			}
			__run_dpkg_command("triggers", command, "");
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

