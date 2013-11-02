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

#include <cmath>
#include <queue>
#include <algorithm>

#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/system/state.hpp>

#include <internal/nativeresolver/impl.hpp>
#include <internal/graph.hpp>

namespace cupt {
namespace internal {

using std::queue;

struct NativeResolverImpl::ResolvedSolution
{
	struct PackageEntry
	{
		IntroducedBy introducedBy;
		bool autoremoved;
	};

	map< const dg::Element*, PackageEntry > elements;
	size_t id;
	size_t level;
	ssize_t score;

	const PackageEntry* getPackageEntry(const dg::Element* element) const
	{
		auto it = elements.find(element);
		return it != elements.end() ? &it->second : nullptr;
	}
};

NativeResolverImpl::NativeResolverImpl(const shared_ptr< const Config >& config, const shared_ptr< const Cache >& cache)
	: __config(config), __cache(cache), __score_manager(*config, cache), __auto_removal_possibility(*__config)
{
	__import_installed_versions();
}

void NativeResolverImpl::__import_installed_versions()
{
	auto versions = __cache->getInstalledVersions();
	for (const auto& version: versions)
	{
		// just moving versions, don't try to install or remove some dependencies
		__old_packages[version->packageName] = version;
		__initial_packages[version->packageName].version = version;
	}

	__import_packages_to_reinstall();
}

void NativeResolverImpl::__import_packages_to_reinstall()
{
	bool debugging = __config->getBool("debug::resolver");

	auto reinstallRequiredPackageNames = __cache->getSystemState()->getReinstallRequiredPackageNames();
	FORIT(packageNameIt, reinstallRequiredPackageNames)
	{
		if (debugging)
		{
			debug2("the package '%s' needs a reinstall", *packageNameIt);
		}

		// this also involves creating new entry in __initial_packages
		auto& targetVersion = __initial_packages[*packageNameIt].version;
		targetVersion = nullptr; // removed by default
	}
}

template < typename... Args >
void __mydebug_wrapper(const NativeResolverImpl::ResolvedSolution& solution, const Args&... args)
{
	__mydebug_wrapper(solution, solution.id, args...);
}

template < typename... Args >
void __mydebug_wrapper(const NativeResolverImpl::ResolvedSolution& solution, size_t id, const Args&... args)
{
	string levelString(solution.level, ' ');
	debug2("%s(%u:%zd) %s", levelString, id, solution.score, format2(args...));
}

// installs new version, but does not sticks it
bool NativeResolverImpl::__prepare_version_no_stick(
		const BinaryVersion* version, dg::InitialPackageEntry& initialPackageEntry)
{
	const string& packageName = version->packageName;
	if (initialPackageEntry.version &&
			initialPackageEntry.version->versionString == version->versionString)
	{
		return true; // there is such version installed already
	}

	if (__config->getBool("debug::resolver"))
	{
		debug2("install package '%s', version '%s'", packageName, version->versionString);
	}
	initialPackageEntry.modified = true;
	initialPackageEntry.version = version;

	return true;
}

void NativeResolverImpl::setAutomaticallyInstalledFlag(const string& packageName, bool flagValue)
{
	__auto_status_overrides[packageName] = flagValue;
}

namespace {

string getAnnotation(const RelationExpression& re, bool invert)
{
	string prefix = !invert ? __("satisfy") : __("unsatisfy");
	return format2("%s '%s'", prefix, re.toString());
};

}

void NativeResolverImpl::satisfyRelationExpression(const RelationExpression& re,
		bool invert, const string& proposedAnnotation, RequestImportance importance, bool asAutomatic)
{
	const string& annotation = !proposedAnnotation.empty() ? proposedAnnotation : getAnnotation(re, invert);
	p_userRelationExpressions.push_back({ re, invert, annotation, importance, asAutomatic });
	if (__config->getBool("debug::resolver"))
	{
		debug2("on request '%s' strictly %ssatisfying relation '%s'", annotation, (invert? "un" : ""), re.toString());
	}
}

void NativeResolverImpl::upgrade()
{
	FORIT(it, __initial_packages)
	{
		dg::InitialPackageEntry& initialPackageEntry = it->second;
		if (!initialPackageEntry.version)
		{
			continue;
		}

		const string& packageName = it->first;
		auto package = __cache->getBinaryPackage(packageName);

		// if there is original version, then the preferred version should exist
		auto supposedVersion = static_cast< const BinaryVersion* >
				(__cache->getPreferredVersion(package));
		if (!supposedVersion)
		{
			fatal2i("supposed version doesn't exist");
		}

		__prepare_version_no_stick(supposedVersion, initialPackageEntry);
	}
}

/*
struct SolutionScoreLess
{
	bool operator()(const shared_ptr< Solution >& left,
			const shared_ptr< Solution >& right) const
	{
		if (left->score < right->score)
		{
			return true;
		}
		if (left->score > right->score)
		{
			return false;
		}
		return left->id > right->id;
	}
};
*/

bool NativeResolverImpl::p_computeTargetAutoStatus(const string& packageName,
		const ResolvedSolution& solution, const dg::Element* elementPtr) const
{
	auto overrideIt = __auto_status_overrides.find(packageName);
	if (overrideIt != __auto_status_overrides.end())
	{
		return overrideIt->second;
	}

	if (__old_packages.count(packageName))
	{
		return __cache->isAutomaticallyInstalled(packageName);
	}

	auto packageEntryPtr = solution.getPackageEntry(elementPtr);
	if (!packageEntryPtr)
	{
		fatal2i("native resolver: new package does not have a package entry");
	}
	if (packageEntryPtr->introducedBy.empty())
	{
		fatal2i("native resolver: new package does not have 'introducedBy'");
	}
	return packageEntryPtr->introducedBy.brokenElementPtr->asAuto();
}

AutoRemovalPossibility::Allow NativeResolverImpl::p_isCandidateForAutoRemoval(
		const ResolvedSolution& solution, const dg::Element* elementPtr)
{
	typedef AutoRemovalPossibility::Allow Allow;

	auto versionVertex = dynamic_cast< const dg::VersionVertex* >(elementPtr);
	if (!versionVertex)
	{
		return Allow::No;
	}

	const string& packageName = versionVertex->getPackageName();
	auto& version = versionVertex->version;

	if (!version)
	{
		return Allow::No;
	}

	return __auto_removal_possibility.isAllowed(version, __old_packages.count(packageName),
			p_computeTargetAutoStatus(packageName, solution, elementPtr));
}

void NativeResolverImpl::__clean_automatically_installed(ResolvedSolution& solution)
{
	typedef AutoRemovalPossibility::Allow Allow;

	map< const dg::Element*, Allow > isCandidateForAutoRemovalCache;
	auto isCandidateForAutoRemoval = [this, &solution, &isCandidateForAutoRemovalCache]
			(const dg::Element* elementPtr) -> Allow
	{
		auto cacheInsertionResult = isCandidateForAutoRemovalCache.insert( { elementPtr, {}});
		auto& answer = cacheInsertionResult.first->second;
		if (cacheInsertionResult.second)
		{
			answer = p_isCandidateForAutoRemoval(solution, elementPtr);
		}
		return answer;
	};

	Graph< const dg::Element* > dependencyGraph;
	auto mainVertexPtr = dependencyGraph.addVertex(NULL);
	const set< const dg::Element* >& vertices = dependencyGraph.getVertices();
	{ // building dependency graph
		for (auto element: solution.elements)
		{
			dependencyGraph.addVertex(element.first);
		}
		FORIT(elementPtrIt, vertices)
		{
			if (!*elementPtrIt)
			{
				continue; // main vertex
			}
			const GraphCessorListType& successorElementPtrs =
					__solution_storage->getSuccessorElements(*elementPtrIt);
			FORIT(successorElementPtrIt, successorElementPtrs)
			{
				if ((*successorElementPtrIt)->isAnti())
				{
					continue;
				}
				const GraphCessorListType& successorSuccessorElementPtrs =
						__solution_storage->getSuccessorElements(*successorElementPtrIt);

				bool allRightSidesAreAutomatic = true;
				const dg::Element*  candidateElementPtr = NULL;
				FORIT(successorSuccessorElementPtrIt, successorSuccessorElementPtrs)
				{
					auto it = vertices.find(*successorSuccessorElementPtrIt);
					if (it != vertices.end())
					{
						switch (isCandidateForAutoRemoval(*it))
						{
							case Allow::No:
								allRightSidesAreAutomatic = false;
								break;
							case Allow::YesIfNoRDepends:
								dependencyGraph.addEdgeFromPointers(*elementPtrIt, *it);
							case Allow::Yes:
								if (!candidateElementPtr) // not found yet
								{
									candidateElementPtr = *it;
								}
								break;
						}
					}
				}
				if (allRightSidesAreAutomatic && candidateElementPtr)
				{
					dependencyGraph.addEdgeFromPointers(*elementPtrIt, candidateElementPtr);
				}
			}

			if (isCandidateForAutoRemoval(*elementPtrIt) == Allow::No)
			{
				dependencyGraph.addEdgeFromPointers(mainVertexPtr, *elementPtrIt);
			}
		}
	}

	{ // looping through the candidates
		bool debugging = __config->getBool("debug::resolver");

		auto reachableElementPtrPtrs = dependencyGraph.getReachableFrom(mainVertexPtr);

		for (auto elementToAutoremove: vertices)
		{
			if (!reachableElementPtrPtrs.count(elementToAutoremove))
			{
				if (debugging)
				{
					debug2("auto-removed '%s'", elementToAutoremove->toString());
				}
				auto emptyElement = __solution_storage->getCorrespondingEmptyElement(elementToAutoremove);
				solution.elements.erase(elementToAutoremove);
				solution.elements[emptyElement].autoremoved = true;
			}
		}
	}
}

/*
void NativeResolverImpl::__pre_apply_action(const Solution& originalSolution,
		Solution& solution, unique_ptr< Action >&& actionToApply, size_t oldSolutionId)
{
	if (originalSolution.finished)
	{
		fatal2i("an attempt to make changes to already finished solution");
	}

	auto oldElementPtr = actionToApply->oldElementPtr;
	auto newElementPtr = actionToApply->newElementPtr;
	const ScoreChange& profit = actionToApply->profit;

	if (__config->getBool("debug::resolver"))
	{
		__mydebug_wrapper(originalSolution, oldSolutionId, "-> (%u,Δ:[%s]) trying: '%s' -> '%s'",
				solution.id, __score_manager.getScoreChangeString(profit),
				oldElementPtr ? oldElementPtr->toString() : "", newElementPtr->toString());
	}

	solution.score += __score_manager.getScoreChangeValue(profit);

	solution.pendingAction = std::forward< unique_ptr< Action >&& >(actionToApply);
}
*/

/*
void NativeResolverImpl::__calculate_profits(vector< unique_ptr< Action > >& actions) const
{
	auto getVersion = [](const dg::Element* elementPtr) -> const BinaryVersion*
	{
		if (!elementPtr)
		{
			return nullptr;
		}
		auto versionVertex = dynamic_cast< const dg::VersionVertex* >(elementPtr);
		if (!versionVertex)
		{
			return nullptr;
		}
		return versionVertex->version;
	};

	size_t position = 0;
	FORIT(actionIt, actions)
	{
		Action& action = **actionIt;

		switch (action.newElementPtr->getUnsatisfiedType())
		{
			case dg::Unsatisfied::None:
				action.profit = __score_manager.getVersionScoreChange(
						getVersion(action.oldElementPtr), getVersion(action.newElementPtr));
				break;
			case dg::Unsatisfied::Recommends:
				action.profit = __score_manager.getUnsatisfiedRecommendsScoreChange();
				break;
			case dg::Unsatisfied::Suggests:
				action.profit = __score_manager.getUnsatisfiedSuggestsScoreChange();
				break;
			case dg::Unsatisfied::Sync:
				action.profit = __score_manager.getUnsatisfiedSynchronizationScoreChange();
				break;
			case dg::Unsatisfied::Custom:
				action.profit = __score_manager.getCustomUnsatisfiedScoreChange(action.newElementPtr->getUnsatisfiedImportance());
				break;
		}
		action.profit.setPosition(position);
		++position;
	}
}
*/

/*
void NativeResolverImpl::__pre_apply_actions_to_solution_tree(
		std::function< void (const shared_ptr< Solution >&) > callback,
		const shared_ptr< Solution >& currentSolution, vector< unique_ptr< Action > >& actions)
{
		auto splitSolution = currentSolution;
		for (auto&& action: actions)
		{
			splitSolution = __solution_storage->fakeCloneSolution(splitSolution);
			__pre_apply_action(*splitSolution, *splitSolution, std::move(action), splitSolution->id);
			__post_apply_action(*__solution_storage, *splitSolution);
		}
		callback(currentSolution);
}
*/

void NativeResolverImpl::__fillSuggestedPackageReasons(const ResolvedSolution& solution,
		const string& packageName, Resolver::SuggestedPackage& suggestedPackage,
		const dg::Element* elementPtr, map< const dg::Element*, size_t >& reasonProcessingCache) const
{
	static const shared_ptr< const Reason > userReason(new UserReason);
	static const shared_ptr< const Reason > autoRemovalReason(new AutoRemovalReason);

	auto fillReasonElements = [&suggestedPackage]
			(const IntroducedBy&, const dg::Element* elementPtr)
	{
		auto versionVertex = static_cast< const dg::VersionVertex* >(elementPtr);
		suggestedPackage.reasonPackageNames.push_back(versionVertex->getPackageName());
	};

	auto packageEntryPtr = solution.getPackageEntry(elementPtr);
	if (packageEntryPtr->autoremoved)
	{
		suggestedPackage.reasons.push_back(autoRemovalReason);
	}
	else
	{
		const auto& introducedBy = packageEntryPtr->introducedBy;
		if (!introducedBy.empty())
		{
			suggestedPackage.reasons.push_back(introducedBy.getReason());
			/* FIXME:
			__solution_storage->processReasonElements(solution, reasonProcessingCache,
					introducedBy, elementPtr, std::cref(fillReasonElements));
			*/
			(void)reasonProcessingCache;
			(void)fillReasonElements;
		}
		auto initialPackageIt = __initial_packages.find(packageName);
		if (initialPackageIt != __initial_packages.end() && initialPackageIt->second.modified)
		{
			suggestedPackage.reasons.push_back(userReason);
		}
	}
}

Resolver::UserAnswer::Type NativeResolverImpl::__propose_solution(
		const ResolvedSolution& solution, Resolver::CallbackType callback, bool trackReasons)
{
	// build "user-frienly" version of solution
	Resolver::Offer offer;
	Resolver::SuggestedPackages& suggestedPackages = offer.suggestedPackages;

	map< const dg::Element*, size_t > reasonProcessingCache;

	for (const auto& item: solution.elements)
	{
		auto elementPtr = item.first;

		auto vertex = dynamic_cast< const dg::VersionVertex* >(elementPtr);
		if (vertex)
		{
			const string& packageName = vertex->getPackageName();
			if (!vertex->version && !__initial_packages.count(packageName))
			{
				continue;
			}

			Resolver::SuggestedPackage& suggestedPackage = suggestedPackages[packageName];
			suggestedPackage.version = vertex->version;

			if (trackReasons)
			{
				__fillSuggestedPackageReasons(solution, packageName, suggestedPackage,
						elementPtr, reasonProcessingCache);
			}
			suggestedPackage.automaticallyInstalledFlag = p_computeTargetAutoStatus(packageName, solution, elementPtr);
		}
		else
		{
			// non-version vertex - unsatisfied one
			for (auto predecessor: __solution_storage->getPredecessorElements(elementPtr))
			{
				for (auto affectedVersionElementPtr: __solution_storage->getPredecessorElements(predecessor))
				{
					if (solution.getPackageEntry(affectedVersionElementPtr))
					{
						offer.unresolvedProblems.push_back(
								predecessor->getReason(*affectedVersionElementPtr));
					}
				}
			}
		}
	}

	// suggest found solution
	bool debugging = __config->getBool("debug::resolver");
	if (debugging)
	{
		__mydebug_wrapper(solution, "proposing this solution");
	}

	auto userAnswer = callback(offer);
	if (debugging)
	{
		if (userAnswer == Resolver::UserAnswer::Accept)
		{
			__mydebug_wrapper(solution, "accepted");
		}
		else if (userAnswer == Resolver::UserAnswer::Decline)
		{
			__mydebug_wrapper(solution, "declined");
		}
	}

	return userAnswer;
}

/*
void NativeResolverImpl::__final_verify_solution(const ResolvedSolution& solution)
{
	for (const auto& item: solution.elements)
	{
		auto element = item.first;
		for (auto successorElement: __solution_storage->getSuccessorElements(element))
		{
			if (!__solution_storage->verifyElement(solution, successorElement))
			{
				fatal2i("final solution check failed: solution '%u', version '%s', problem '%s'",
						solution.id, element->toString(), successorElement->toString());
			}
		}
	}
}
*/

/*
BrokenPair __get_broken_pair(const SolutionStorage& solutionStorage, const Solution& solution)
{
	auto compareBrokenSuccessors = [](const BrokenSuccessor& left, const BrokenSuccessor& right)
	{
		auto leftTypePriority = left.elementPtr->getTypePriority();
		auto rightTypePriority = right.elementPtr->getTypePriority();
		if (leftTypePriority < rightTypePriority)
		{
			return true;
		}
		if (leftTypePriority > rightTypePriority)
		{
			return false;
		}

		if (left.priority < right.priority)
		{
			return true;
		}
		if (left.priority > right.priority)
		{
			return false;
		}

		return left.elementPtr->id < right.elementPtr->id;
	};

	const auto& brokenSuccessors = solution.getBrokenSuccessors();
	auto bestBrokenSuccessorIt = std::max_element(
			brokenSuccessors.begin(), brokenSuccessors.end(), compareBrokenSuccessors);
	if (bestBrokenSuccessorIt == brokenSuccessors.end())
	{
		return BrokenPair{ nullptr, { nullptr, 0 } };
	}
	BrokenPair result = { nullptr, *bestBrokenSuccessorIt };
	for (auto reverseDependencyPtr: solutionStorage.getPredecessorElements(bestBrokenSuccessorIt->elementPtr))
	{
		if (solution.getPackageEntry(reverseDependencyPtr))
		{
			if (!result.versionElementPtr || (result.versionElementPtr->id < reverseDependencyPtr->id))
			{
				result.versionElementPtr = reverseDependencyPtr;
			}
		}
	}
	if (!result.versionElementPtr)
	{
		fatal2i("__get_broken_pair: no existing in the solution predecessors for the broken successor '%s'",
				bestBrokenSuccessorIt->elementPtr->toString());
	}

	return result;
}
*/

/*
	if (currentSolution->pendingAction)
	{
		currentSolution->prepare();
	}
*/

struct SolutionGraphItem
{
	enum Status { SplitPending, ReducePending, Opened, Success, Fail };

	Solution solution;
	mutable Status status;
	mutable ssize_t score;
	mutable vector< const dg::Element* > stickedElements;

	SolutionGraphItem(const Solution& solution_, Status status_)
		: solution(solution_)
		, status(status_)
	{}

	bool operator<(const SolutionGraphItem& other) const
	{
		return solution < other.solution;
	}
};
typedef Graph< SolutionGraphItem > SolutionGraph;

namespace {

typedef SolutionGraphItem::Status IS;

void resolveGraphItem(SolutionGraph*, const SolutionGraphItem*);

IS computeGraphItemSplitStatus(SolutionGraph* graph, const SolutionGraphItem* item)
{
	for (const auto& solution: item->solution.split())
	{
		auto newSuccessor = graph->addVertex({ solution, IS::ReducePending });
		graph->addEdgeFromPointers(item, newSuccessor);
		if (newSuccessor->status == IS::Fail)
		{
			return IS::Fail;
		}
	}

	for (auto child: graph->getSuccessorsFromPointer(item))
	{
		resolveGraphItem(graph, child);
		if (child->status == IS::Fail)
		{
			return IS::Fail;
		}
	}

	// at this point all children have IS::Success
	item->score = 0;
	for (auto child: graph->getSuccessorsFromPointer(item))
	{
		item->score += child->score;
		item->stickedElements.insert(item->stickedElements.end(),
			child->stickedElements.begin(), child->stickedElements.end());
	}
	return IS::Success;
}

IS computeGraphItemReduceStatus(SolutionGraph* graph, const SolutionGraphItem* item)
{
	for (const auto& solution: item->solution.reduce())
	{
		auto newSuccessor = graph->addVertex({ solution, IS::SplitPending });
		graph->addEdgeFromPointers(item, newSuccessor);
	}

	IS result = IS::Fail;
	for (auto child: graph->getSuccessorsFromPointer(item))
	{
		resolveGraphItem(graph, child);
		if (child->status == IS::Success)
		{
			if (result == IS::Fail || (child->score > item->score))
			{
				item->score = child->score;
				item->stickedElements = child->stickedElements;
				result = IS::Success;
			}
		}
	}

	return result;
}

// postcondition: item->status > IS::Opened
void resolveGraphItem(SolutionGraph* graph, const SolutionGraphItem* item)
{
	debug2("solving: %s", item->solution.toString());
	if (item->status > IS::Opened) return;

	if (auto finishedElement = item->solution.getFinishedElement())
	{
		item->stickedElements.push_back(finishedElement);
		item->score = 7; // FIXME: finishedElement->getScore();
		item->status = IS::Success;
	}
	else if (item->status == IS::SplitPending)
	{
		item->status = IS::Opened;
		item->status = computeGraphItemSplitStatus(graph, item);
	}
	else if (item->status == IS::ReducePending)
	{
		item->status = IS::Opened;
		item->status = computeGraphItemReduceStatus(graph, item);
	}
	else
	{
		fatal2i("native resolver: p_resolveGraphItem: trying to resolve WIP-item");
	}
	debug2("solved: %s -> %s", item->solution.toString(),
			(item->status == IS::Success ? format2("success (%zu)", item->score) : "fail"));
}

}

/*
				if (debugging)
				{
					__mydebug_wrapper(*currentSolution, "no solutions");
				}
*/
/*
			if (debugging)
			{
				__mydebug_wrapper(*currentSolution, "problem (%zu:%zu): %s: %s",
						bp.brokenSuccessor.elementPtr->getTypePriority(), bp.brokenSuccessor.priority,
						bp.versionElementPtr->toString(), bp.brokenSuccessor.elementPtr->toString());
			}
*/

/*
				__calculate_profits(possibleActions);
*/

bool NativeResolverImpl::resolve(Resolver::CallbackType callback)
{
	const bool debugging = __config->getBool("debug::resolver");
	const bool trackReasons = __config->getBool("cupt::resolver::track-reasons");

	if (debugging) debug2("started resolving");

	SolutionGraph solutionGraph;

	Solution initialSolution;
	__solution_storage.reset(new SolutionStorage(*__config, *__cache));
	__solution_storage->prepareForResolving(initialSolution,
			__old_packages, __initial_packages, p_userRelationExpressions);

	auto* initialItem = solutionGraph.addVertex({initialSolution, SolutionGraphItem::Status::SplitPending});
	resolveGraphItem(&solutionGraph, initialItem);

	if (initialItem->status == SolutionGraphItem::Status::Success)
	{
		ResolvedSolution resolvedSolution;
		for (auto element: initialItem->stickedElements)
		{
			resolvedSolution.elements[element];
		}

		__clean_automatically_installed(resolvedSolution);

		//__final_verify_solution(*currentSolution);

		auto userAnswer = __propose_solution(resolvedSolution, callback, trackReasons);
		switch (userAnswer)
		{
			case Resolver::UserAnswer::Accept:
				// yeah, this is end of our tortures
				return true;
			case Resolver::UserAnswer::Decline:
				// now same as abandon, user should specify more precise request instead
			case Resolver::UserAnswer::Abandon:
				// user has selected abandoning all further efforts
				return false;
		}
	}
	else
	{
		fatal2(__("unable to resolve dependencies, because of:\n\n%s"),
				// FIXME: __decision_fail_tree.toString());
				"<not implemented>");
	}
	return false; // unreachable
}

}
}

