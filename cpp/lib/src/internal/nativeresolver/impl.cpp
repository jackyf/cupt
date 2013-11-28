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
void __mydebug_wrapper(const Solution& solution, const Args&... args)
{
	__mydebug_wrapper(solution, solution.id, args...);
}

template < typename... Args >
void __mydebug_wrapper(const Solution& solution, size_t id, const Args&... args)
{
	string levelString(solution.getLevel(), ' ');
	debug2("%s(%u:%zd) %s", levelString, id, solution.getScore(), format2(args...));
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

struct SolutionScoreLess
{
	bool operator()(const shared_ptr< Solution >& left,
			const shared_ptr< Solution >& right) const
	{
		if (left->getScore() < right->getScore())
		{
			return true;
		}
		if (left->getScore() > right->getScore())
		{
			return false;
		}
		return left->id > right->id;
	}
};
typedef set< shared_ptr< Solution >, SolutionScoreLess > SolutionContainer;
typedef std::function< SolutionContainer::iterator (SolutionContainer&) > SolutionChooser;

SolutionContainer::iterator __fair_chooser(SolutionContainer& solutions)
{
	// choose the solution with maximum score
	return --solutions.end();
}

SolutionContainer::iterator __full_chooser(SolutionContainer& solutions)
{
	// defer the decision until all solutions are built
	FORIT(solutionIt, solutions)
	{
		if (! (*solutionIt)->isFinished())
		{
			return solutionIt;
		}
	}

	// heh, the whole solution tree has been already built?.. ok, let's choose
	// the best solution
	return __fair_chooser(solutions);
}

bool NativeResolverImpl::p_computeTargetAutoStatus(const string& packageName,
		const PreparedSolution& solution, const dg::Element* elementPtr) const
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
		const PreparedSolution& solution, const dg::Element* elementPtr)
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

bool NativeResolverImpl::__clean_automatically_installed(PreparedSolution& solution)
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
		auto elementPtrs = solution.getElements();
		FORIT(elementPtrIt, elementPtrs)
		{
			dependencyGraph.addVertex(*elementPtrIt);
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
				const dg::Element* const* candidateElementPtrPtr = NULL;
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
								dependencyGraph.addEdge(&*elementPtrIt, &*it);
							case Allow::Yes:
								if (!candidateElementPtrPtr) // not found yet
								{
									candidateElementPtrPtr = &*it;
								}
								break;
						}
					}
				}
				if (allRightSidesAreAutomatic && candidateElementPtrPtr)
				{
					dependencyGraph.addEdge(&*elementPtrIt, candidateElementPtrPtr);
				}
			}

			if (isCandidateForAutoRemoval(*elementPtrIt) == Allow::No)
			{
				dependencyGraph.addEdge(mainVertexPtr, &*elementPtrIt);
			}
		}
	}

	{ // looping through the candidates
		bool debugging = __config->getBool("debug::resolver");

		auto reachableElementPtrPtrs = dependencyGraph.getReachableFrom(*mainVertexPtr);

		FORIT(elementPtrIt, vertices)
		{
			if (!reachableElementPtrPtrs.count(&*elementPtrIt))
			{
				auto emptyElementPtr = __solution_storage->getCorrespondingEmptyElement(*elementPtrIt);

				PackageEntry packageEntry;
				packageEntry.autoremoved = true;

				if (debugging)
				{
					__mydebug_wrapper(solution, "auto-removed '%s'", (*elementPtrIt)->toString());
				}
				__solution_storage->setPackageEntry(solution, emptyElementPtr,
						std::move(packageEntry), *elementPtrIt);
			}
		}
	}
	return true;
}

SolutionChooser __select_solution_chooser(const Config& config)
{
	SolutionChooser result;

	auto resolverType = config.getString("cupt::resolver::type");
	if (resolverType == "fair")
	{
		result = __fair_chooser;
	}
	else if (resolverType == "full")
	{
		result = __full_chooser;
	}
	else
	{
		fatal2(__("wrong resolver type '%s'"), resolverType);
	}

	return result;
}

/* __pre_apply_action only prints debug info and changes level/score of the
   solution, not necessarily modifying packages in it, saving RAM and CPU */
void NativeResolverImpl::__pre_apply_action(const Solution& originalSolution,
		Solution& solution, unique_ptr< Action >&& actionToApply,
		size_t actionPosition, size_t oldSolutionId)
{
	if (originalSolution.isFinished())
	{
		fatal2i("an attempt to make changes to already finished solution");
	}

	auto oldElementPtr = actionToApply->oldElementPtr;
	auto newElementPtr = actionToApply->newElementPtr;
	auto scoreChange = p_getScoreChange(oldElementPtr, newElementPtr, actionPosition);

	if (__config->getBool("debug::resolver"))
	{
		__mydebug_wrapper(originalSolution, oldSolutionId, "-> (%u,Δ:[%s]) trying: '%s' -> '%s'",
				solution.id, __score_manager.getScoreChangeString(scoreChange),
				oldElementPtr ? oldElementPtr->toString() : "", newElementPtr->toString());
	}

	solution.score += __score_manager.getScoreChangeValue(scoreChange);

	__solution_storage->assignAction(solution, std::move(actionToApply));
}

ScoreChange NativeResolverImpl::p_getScoreChange(
		const dg::Element* oldElement, const dg::Element* newElement, size_t position) const
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

	ScoreChange result;
	switch (newElement->getUnsatisfiedType())
	{
		case dg::Unsatisfied::None:
			result = __score_manager.getVersionScoreChange(getVersion(oldElement), getVersion(newElement));
			break;
		case dg::Unsatisfied::Recommends:
			result = __score_manager.getUnsatisfiedRecommendsScoreChange();
			break;
		case dg::Unsatisfied::Suggests:
			result = __score_manager.getUnsatisfiedSuggestsScoreChange();
			break;
		case dg::Unsatisfied::Sync:
			result = __score_manager.getUnsatisfiedSynchronizationScoreChange();
			break;
		case dg::Unsatisfied::Custom:
			result = __score_manager.getCustomUnsatisfiedScoreChange(newElement->getUnsatisfiedImportance());
			break;
	}
	result.setPosition(position);

	return result;
}

void NativeResolverImpl::__pre_apply_actions_to_solution_tree(
		std::function< void (const shared_ptr< Solution >&) > callback,
		const shared_ptr< PreparedSolution >& currentSolution, vector< unique_ptr< Action > >& actions)
{
	// apply all the solutions by one
	bool onlyOneAction = (actions.size() == 1);
	auto oldSolutionId = currentSolution->id;
	size_t position = 0;
	FORIT(actionIt, actions)
	{
		auto newSolution = onlyOneAction ?
				__solution_storage->fakeCloneSolution(currentSolution) :
				__solution_storage->cloneSolution(currentSolution);
		__pre_apply_action(*currentSolution, *newSolution,
				std::move(*actionIt), position++, oldSolutionId);
		callback(newSolution);
	}
}

void __erase_worst_solutions(SolutionContainer& solutions,
		size_t maxSolutionCount, bool debugging, bool& thereWereDrops)
{
	// don't allow solution tree to grow unstoppably
	while (solutions.size() > maxSolutionCount)
	{
		// drop the worst solution
		auto worstSolutionIt = solutions.begin();
		if (debugging)
		{
			__mydebug_wrapper(**worstSolutionIt, "dropped");
		}
		solutions.erase(worstSolutionIt);
		if (!thereWereDrops)
		{
			thereWereDrops = true;
			warn2(__("some solutions were dropped, you may want to increase the value of the '%s' option"),
					"cupt::resolver::max-solution-count");
		}
	}
}

bool NativeResolverImpl::__makes_sense_to_modify_package(const PreparedSolution& solution,
		const dg::Element* candidateElementPtr, const dg::Element* brokenElementPtr,
		bool debugging)
{

	__solution_storage->unfoldElement(candidateElementPtr);

	const GraphCessorListType& successorElementPtrs =
			__solution_storage->getSuccessorElements(candidateElementPtr);
	FORIT(successorElementPtrIt, successorElementPtrs)
	{
		if (*successorElementPtrIt == brokenElementPtr)
		{
			if (debugging)
			{
				__mydebug_wrapper(solution, "not considering %s: it has the same problem",
						candidateElementPtr->toString());
			}
			return false;
		}
	}

	// let's try even harder to find if this candidate is really appropriate for us
	auto brokenElementTypePriority = brokenElementPtr->getTypePriority();
	const GraphCessorListType& brokenElementSuccessorElementPtrs =
			__solution_storage->getSuccessorElements(brokenElementPtr);
	FORIT(successorElementPtrIt, successorElementPtrs)
	{
		/* we check only successors with the same or bigger priority than
		   currently broken one */
		if ((*successorElementPtrIt)->getTypePriority() < brokenElementTypePriority)
		{
			continue;
		}
		/* if any of such successors gives us equal or less "space" in
		   terms of satisfying elements, the version won't be accepted as a
		   resolution */
		const GraphCessorListType& successorElementSuccessorElementPtrs =
				__solution_storage->getSuccessorElements(*successorElementPtrIt);

		bool isMoreWide = false;
		FORIT(elementPtrIt, successorElementSuccessorElementPtrs)
		{
			bool notFound = (std::find(brokenElementSuccessorElementPtrs.begin(),
					brokenElementSuccessorElementPtrs.end(), *elementPtrIt)
					== brokenElementSuccessorElementPtrs.end());

			if (notFound)
			{
				// more wide relation, can't say nothing bad with it at time being
				isMoreWide = true;
				break;
			}
		}

		if (!isMoreWide)
		{
			if (debugging)
			{
				__mydebug_wrapper(solution, "not considering %s: it contains equal or less wide relation expression '%s'",
						candidateElementPtr->toString(), (*successorElementPtrIt)->toString());
			}
			return false;
		}
	}

	return true;
}

void NativeResolverImpl::__add_actions_to_modify_package_entry(
		vector< unique_ptr< Action > >& actions, const PreparedSolution& solution,
		const dg::Element* versionElementPtr, const dg::Element* brokenElementPtr,
		bool debugging)
{
	auto versionPackageEntryPtr = solution.getPackageEntry(versionElementPtr);
	if (versionPackageEntryPtr->sticked)
	{
		return;
	}

	const forward_list< const dg::Element* >& conflictingElementPtrs =
			__solution_storage->getConflictingElements(versionElementPtr);
	FORIT(conflictingElementPtrIt, conflictingElementPtrs)
	{
		if (*conflictingElementPtrIt == versionElementPtr)
		{
			continue;
		}
		if (!versionPackageEntryPtr->isModificationAllowed(*conflictingElementPtrIt))
		{
			continue;
		}
		if (__makes_sense_to_modify_package(solution, *conflictingElementPtrIt,
				brokenElementPtr, debugging))
		{
			// other version seems to be ok
			unique_ptr< Action > action(new Action);
			action->oldElementPtr = versionElementPtr;
			action->newElementPtr = *conflictingElementPtrIt;

			actions.push_back(std::move(action));
		}
	}
}

void NativeResolverImpl::__add_actions_to_fix_dependency(vector< unique_ptr< Action > >& actions,
		const PreparedSolution& solution, const dg::Element* brokenElementPtr)
{
	const GraphCessorListType& successorElementPtrs =
			__solution_storage->getSuccessorElements(brokenElementPtr);
	// install one of versions package needs
	FORIT(successorElementPtrIt, successorElementPtrs)
	{
		const dg::Element* conflictingElementPtr;
		if (__solution_storage->simulateSetPackageEntry(solution, *successorElementPtrIt, &conflictingElementPtr))
		{
			unique_ptr< Action > action(new Action);
			action->oldElementPtr = conflictingElementPtr;
			action->newElementPtr = *successorElementPtrIt;

			actions.push_back(std::move(action));
		}
	}
}

void NativeResolverImpl::__prepare_reject_requests(vector< unique_ptr< Action > >& actions) const
{
	if (actions.size() <= 1) return;

	auto allNewElements = std::make_shared< vector< const dg::Element* > >();
	allNewElements->reserve(actions.size());

	for (const auto& action: actions)
	{
		action->allActionNewElements = allNewElements;
		allNewElements->push_back(action->newElementPtr);
	}
}

void NativeResolverImpl::__fillSuggestedPackageReasons(const PreparedSolution& solution,
		const string& packageName, Resolver::SuggestedPackage& suggestedPackage,
		const dg::Element* elementPtr) const
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
			__solution_storage->processReasonElements(solution,
					introducedBy, elementPtr, std::cref(fillReasonElements));
		}
		auto initialPackageIt = __initial_packages.find(packageName);
		if (initialPackageIt != __initial_packages.end() && initialPackageIt->second.modified)
		{
			suggestedPackage.reasons.push_back(userReason);
		}
	}
}

Resolver::UserAnswer::Type NativeResolverImpl::__propose_solution(
		const PreparedSolution& solution, Resolver::CallbackType callback, bool trackReasons)
{
	// build "user-frienly" version of solution
	Resolver::Offer offer;
	Resolver::SuggestedPackages& suggestedPackages = offer.suggestedPackages;

	for (auto elementPtr: solution.getElements())
	{
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
				__fillSuggestedPackageReasons(solution, packageName, suggestedPackage, elementPtr);
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

struct BrokenPair
{
	const dg::Element* versionElementPtr;
	BrokenSuccessor brokenSuccessor;
};

void NativeResolverImpl::__generate_possible_actions(vector< unique_ptr< Action > >* possibleActionsPtr,
		const PreparedSolution& solution, const BrokenPair& bp, bool debugging)
{
	auto brokenElementPtr = bp.brokenSuccessor.elementPtr;

	__add_actions_to_fix_dependency(*possibleActionsPtr, solution, brokenElementPtr);
	__add_actions_to_modify_package_entry(*possibleActionsPtr, solution,
			bp.versionElementPtr, brokenElementPtr, debugging);

	for (auto& action: *possibleActionsPtr)
	{
		action->brokenElementPriority = bp.brokenSuccessor.priority;
	}
}

void NativeResolverImpl::__final_verify_solution(const PreparedSolution& solution)
{
	for (auto element: solution.getElements())
	{
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

BrokenPair __get_broken_pair(const SolutionStorage& solutionStorage,
		const PreparedSolution& solution, const map< const dg::Element*, size_t >& failCounts)
{
	auto failValue = [&failCounts](const dg::Element* e) -> size_t
	{
		auto it = failCounts.find(e);
		return it != failCounts.end() ? it->second : 0u;
	};
	auto compareBrokenSuccessors = [&failValue](const BrokenSuccessor& left, const BrokenSuccessor& right)
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

		auto leftFailValue = failValue(left.elementPtr);
		auto rightFailValue = failValue(right.elementPtr);
		if (leftFailValue < rightFailValue)
		{
			return true;
		}
		if (leftFailValue > rightFailValue)
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

shared_ptr< PreparedSolution > __get_next_current_solution(
		SolutionContainer& solutions, SolutionStorage& solutionStorage, const SolutionChooser& chooser)
{
	auto currentSolutionIt = chooser(solutions);
	shared_ptr< Solution > currentSolution = *currentSolutionIt;
	solutions.erase(currentSolutionIt);

	return solutionStorage.prepareSolution(currentSolution);
}

void NativeResolverImpl::__fill_and_process_introduced_by(
		const PreparedSolution& solution, const BrokenPair& bp, ActionContainer* actionsPtr)
{
	IntroducedBy ourIntroducedBy;
	ourIntroducedBy.versionElementPtr = bp.versionElementPtr;
	ourIntroducedBy.brokenElementPtr = bp.brokenSuccessor.elementPtr;

	if (actionsPtr->empty() && !__any_solution_was_found)
	{
		__decision_fail_tree.addFailedSolution(*__solution_storage, solution, ourIntroducedBy);
	}
	else
	{
		for (const auto& actionPtr: *actionsPtr)
		{
			actionPtr->introducedBy = ourIntroducedBy;
		}
	}
}

bool NativeResolverImpl::resolve(Resolver::CallbackType callback)
{
	auto solutionChooser = __select_solution_chooser(*__config);

	const bool debugging = __config->getBool("debug::resolver");
	const bool trackReasons = __config->getBool("cupt::resolver::track-reasons");
	const size_t maxSolutionCount = __config->getInteger("cupt::resolver::max-solution-count");
	bool thereWereSolutionsDropped = false;

	if (debugging) debug2("started resolving");

	__any_solution_was_found = false;
	__decision_fail_tree.clear();

	auto initialSolution = std::make_shared< PreparedSolution >();
	__solution_storage.reset(new SolutionStorage(*__config, *__cache));
	__solution_storage->prepareForResolving(*initialSolution,
			__old_packages, __initial_packages, p_userRelationExpressions);

	SolutionContainer solutions = { initialSolution };

	// for each package entry 'count' will contain the number of failures
	// during processing these packages
	map< const dg::Element*, size_t > failCounts;

	while (!solutions.empty())
	{
		vector< unique_ptr< Action > > possibleActions;

		auto currentSolution = __get_next_current_solution(solutions, *__solution_storage, solutionChooser);

		auto problemFound = [this, &failCounts, &possibleActions, &currentSolution, debugging]
		{
			auto bp = __get_broken_pair(*__solution_storage, *currentSolution, failCounts);
			if (!bp.versionElementPtr) return false;

			if (debugging)
			{
				__mydebug_wrapper(*currentSolution, "problem (%zu:%zu): %s: %s",
						bp.brokenSuccessor.elementPtr->getTypePriority(), bp.brokenSuccessor.priority,
						bp.versionElementPtr->toString(), bp.brokenSuccessor.elementPtr->toString());
			}
			__generate_possible_actions(&possibleActions, *currentSolution, bp, debugging);
			__fill_and_process_introduced_by(*currentSolution, bp, &possibleActions);

			// mark package as failed one more time
			failCounts[bp.brokenSuccessor.elementPtr] += 1;

			return true;
		};

		if (!problemFound())
		{
			// if the solution was only just finished
			if (!currentSolution->finished)
			{
				if (debugging)
				{
					__mydebug_wrapper(*currentSolution, "finished");
				}
				currentSolution->finished = 1;
			}

			// resolver can refuse the solution
			solutions.insert(currentSolution);
			auto newSelectedSolutionIt = solutionChooser(solutions);
			if (*newSelectedSolutionIt != currentSolution)
			{
				continue; // ok, process other solution
			}
			solutions.erase(newSelectedSolutionIt);

			// clean up automatically installed by resolver and now unneeded packages
			if (!__clean_automatically_installed(*currentSolution))
			{
				if (debugging)
				{
					__mydebug_wrapper(*currentSolution, "auto-discarded");
				}
				continue;
			}

			if (!__any_solution_was_found)
			{
				__any_solution_was_found = true;
				__decision_fail_tree.clear(); // no need to store this tree anymore
			}

			__final_verify_solution(*currentSolution);

			auto userAnswer = __propose_solution(*currentSolution, callback, trackReasons);
			switch (userAnswer)
			{
				case Resolver::UserAnswer::Accept:
					// yeah, this is end of our tortures
					return true;
				case Resolver::UserAnswer::Abandon:
					// user has selected abandoning all further efforts
					return false;
				case Resolver::UserAnswer::Decline:
					; // caller hasn't accepted this solution, well, go next...
			}
		}
		else
		{
			__prepare_reject_requests(possibleActions);

			if (possibleActions.empty())
			{
				if (debugging)
				{
					__mydebug_wrapper(*currentSolution, "no solutions");
				}
			}
			else
			{
				auto callback = [&solutions](const shared_ptr< Solution >& solution)
				{
					solutions.insert(solution);
				};
				__pre_apply_actions_to_solution_tree(callback, currentSolution, possibleActions);

				__erase_worst_solutions(solutions, maxSolutionCount, debugging, thereWereSolutionsDropped);
			}
		}
	}
	if (!__any_solution_was_found)
	{
		// no solutions pending, we have a great fail
		fatal2(__("unable to resolve dependencies, because of:\n\n%s"),
				__decision_fail_tree.toString());
	}
	return false;
}

}
}

