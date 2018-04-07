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
	p_debugging = __config->getBool("debug::resolver");
	__import_installed_versions();
}

void NativeResolverImpl::__import_installed_versions()
{
	auto versions = __cache->getInstalledVersions();
	for (const auto& version: versions)
	{
		// just moving versions, don't try to install or remove some dependencies
		__old_packages[version->packageName] = version;
	}

	__import_packages_to_reinstall();
}

void NativeResolverImpl::__import_packages_to_reinstall()
{
	auto reinstallRequiredPackageNames = __cache->getSystemState()->getReinstallRequiredPackageNames();
	for (const auto& packageName: reinstallRequiredPackageNames)
	{
		if (p_debugging)
		{
			debug2("the package '%s' needs a reinstall", packageName);
		}

		const string annotation = "reinstall " + packageName;

		auto installedVersion = __cache->getBinaryPackage(packageName)->getInstalledVersion();
		RelationExpression installedExpression(format2("%s (= %s)", packageName, installedVersion->versionString));
		installedExpression[0].relationType = Relation::Types::LiteralyEqual;

		satisfyRelationExpression(installedExpression, true, annotation, RequestImportance::Try, true);
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
	if (p_debugging)
	{
		debug2("on request '%s' strictly %ssatisfying relation '%s'", annotation, (invert? "un" : ""), re.toString());
	}
}

RelationExpression getNotHigherThanInstalledPinRelationExpression(const Cache& cache, const string& packageName)
{
	RelationExpression result;

	auto package = cache.getBinaryPackage(packageName);
	auto sortedPinnedVersions = cache.getSortedVersionsWithPriorities(package);
	auto installedVersion = package->getInstalledVersion();

	if (sortedPinnedVersions.front().version == installedVersion) return result;

	for (auto it = sortedPinnedVersions.rbegin(); it != sortedPinnedVersions.rend(); ++it)
	{
		Relation relation({ packageName.data(), packageName.data()+packageName.size() });
		relation.relationType = Relation::Types::LiteralyEqual;
		relation.versionString = it->version->versionString;
		result.push_back(std::move(relation));

		if (it->version == installedVersion) break;
	}

	return result;
}

void NativeResolverImpl::upgrade()
{
	for (const auto& item: __old_packages)
	{
		const string& packageName = item.first;

		RelationExpression notUpgradeExpression = getNotHigherThanInstalledPinRelationExpression(*__cache, packageName);
		if (notUpgradeExpression.empty()) continue;

		const string annotation = string("upgrade ") + packageName;
		satisfyRelationExpression(notUpgradeExpression, true, annotation, RequestImportance::Wish, true);
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
		const PreparedSolution& solution, dg::Element element) const
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

	auto packageEntryPtr = solution.getPackageEntry(element);
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
		const PreparedSolution& solution, dg::Element element)
{
	typedef AutoRemovalPossibility::Allow Allow;

	auto versionVertex = dynamic_cast< const dg::VersionVertex* >(element);
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
			p_computeTargetAutoStatus(packageName, solution, element));
}

bool NativeResolverImpl::__clean_automatically_installed(PreparedSolution& solution)
{
	typedef AutoRemovalPossibility::Allow Allow;

	map< dg::Element, Allow > isCandidateForAutoRemovalCache;
	auto isCandidateForAutoRemoval = [this, &solution, &isCandidateForAutoRemovalCache]
			(dg::Element element) -> Allow
	{
		auto cacheInsertionResult = isCandidateForAutoRemovalCache.insert( { element, {}});
		auto& answer = cacheInsertionResult.first->second;
		if (cacheInsertionResult.second)
		{
			answer = p_isCandidateForAutoRemoval(solution, element);
		}
		return answer;
	};

	Graph<dg::Element> dependencyGraph;
	auto mainVertexPtr = dependencyGraph.addVertex(nullptr);
	const set<dg::Element>& vertices = dependencyGraph.getVertices();
	{ // building dependency graph
		for (auto packageEntry: solution.getEntries())
		{
			dependencyGraph.addVertex(packageEntry->element);
		}
		for (const auto& element: vertices)
		{
			if (!element) continue; // main vertex

			for (auto successorElement: __solution_storage->getSuccessorElements(element))
			{
				if (successorElement->isAnti()) continue;

				bool allRightSidesAreAutomatic = true;
				const dg::Element* candidateElementPtr = nullptr;
				for (auto successorSuccessorElement: __solution_storage->getSuccessorElements(successorElement))
				{
					auto it = vertices.find(successorSuccessorElement);
					if (it != vertices.end())
					{
						switch (isCandidateForAutoRemoval(*it))
						{
							case Allow::No:
								allRightSidesAreAutomatic = false;
								break;
							case Allow::YesIfNoRDepends:
								dependencyGraph.addEdge(&element, &*it);
							case Allow::Yes:
								if (!candidateElementPtr) // not found yet
								{
									candidateElementPtr = &*it;
								}
								break;
						}
					}
				}
				if (allRightSidesAreAutomatic && candidateElementPtr)
				{
					dependencyGraph.addEdge(&element, candidateElementPtr);
				}
			}

			if (isCandidateForAutoRemoval(element) == Allow::No)
			{
				dependencyGraph.addEdge(mainVertexPtr, &element);
			}
		}
	}

	{ // looping through the candidates
		auto reachableElementPtrs = dependencyGraph.getReachableFrom(*mainVertexPtr);

		for (const auto& element: vertices)
		{
			if (!reachableElementPtrs.count(&element))
			{
				__solution_storage->setEmpty(solution, element);
				if (p_debugging)
				{
					__mydebug_wrapper(solution, "auto-removed '%s'", element->toString());
				}

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

	if (p_debugging)
	{
		__mydebug_wrapper(originalSolution, oldSolutionId, "-> (%u,Δ:[%s]) trying: '%s' -> '%s'",
				solution.id, __score_manager.getScoreChangeString(scoreChange),
				oldElementPtr ? oldElementPtr->toString() : "", newElementPtr->toString());
	}

	solution.score += __score_manager.getScoreChangeValue(scoreChange);

	__solution_storage->assignAction(solution, std::move(actionToApply));
}

ScoreChange NativeResolverImpl::p_getScoreChange(
		dg::Element oldElement, dg::Element newElement, size_t position) const
{
	auto getVersion = [](dg::Element element) -> const BinaryVersion*
	{
		auto versionVertex = dynamic_cast< const dg::VersionVertex* >(element);
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

static inline void checkLeafLimit(size_t leafCount, size_t maxLeafCount)
{
	if (leafCount > maxLeafCount)
	{
		fatal2(__("leaf count limit exceeded"));
	}
}

void NativeResolverImpl::__pre_apply_actions_to_solution_tree(
		std::function< void (const shared_ptr< Solution >&) > callback,
		const shared_ptr< PreparedSolution >& currentSolution, vector< unique_ptr< Action > >& actions)
{
	// apply all the solutions by one
	bool onlyOneAction = (actions.size() == 1);
	auto oldSolutionId = currentSolution->id;
	size_t position = 0;
	for (auto& action: actions)
	{
		auto newSolution = onlyOneAction ?
				__solution_storage->fakeCloneSolution(currentSolution) :
				__solution_storage->cloneSolution(currentSolution);
		checkLeafLimit(newSolution->id, p_maxLeafCount);
		__pre_apply_action(*currentSolution, *newSolution,
				std::move(action), position++, oldSolutionId);
		callback(newSolution);
	}
}

static bool isRelationMoreWide(const SolutionStorage& solutionStorage, const PreparedSolution& solution,
		const GraphCessorListType& successorElementSuccessorElements, const GraphCessorListType& brokenElementSuccessorElements)
{
	const auto& bese = brokenElementSuccessorElements;

	for (auto element: successorElementSuccessorElements)
	{
		dg::Element conflictingElement;
		if (!solutionStorage.simulateSetPackageEntry(solution, element, &conflictingElement) &&
				conflictingElement != element)
		{
			continue;
		}

		bool notFound = (std::find(bese.begin(), bese.end(), element) == bese.end());
		if (notFound)
		{
			return true;
		}
	}
	return false;
}

bool NativeResolverImpl::__makes_sense_to_modify_package(const PreparedSolution& solution,
		dg::Element candidateElement, dg::Element brokenElement)
{
	__solution_storage->unfoldElement(candidateElement);

	const auto& successorElements = __solution_storage->getSuccessorElements(candidateElement);
	for (auto successorElement: successorElements)
	{
		if (successorElement == brokenElement)
		{
			if (p_debugging)
			{
				__mydebug_wrapper(solution, "not considering %s: it has the same problem", candidateElement->toString());
			}
			return false;
		}
	}

	// let's try even harder to find if this candidate is really appropriate for us
	auto brokenElementTypePriority = brokenElement->getTypePriority();
	const auto& brokenElementSuccessorElements = __solution_storage->getSuccessorElements(brokenElement);
	for (auto successorElement: successorElements)
	{
		// we check only successors with the same or bigger priority than currently broken one
		if (successorElement->getTypePriority() < brokenElementTypePriority)
		{
			continue;
		}
		/* if any of such successors gives us equal or less "space" in
		   terms of satisfying elements, the version won't be accepted as a resolution */
		if (!isRelationMoreWide(*__solution_storage, solution,
					__solution_storage->getSuccessorElements(successorElement), brokenElementSuccessorElements))
		{
			if (p_debugging)
			{
				__mydebug_wrapper(solution, "not considering %s: it contains equal or less wide relation expression '%s'",
						candidateElement->toString(), successorElement->toString());
			}
			return false;
		}
	}

	return true;
}

void NativeResolverImpl::__add_actions_to_modify_package_entry(
		vector< unique_ptr< Action > >& actions, const PreparedSolution& solution,
		dg::Element versionElement, dg::Element brokenElement)
{
	auto versionPackageEntryPtr = solution.getPackageEntry(versionElement);
	if (versionPackageEntryPtr->sticked)
	{
		return;
	}

	for (auto conflictingElement: __solution_storage->getConflictingElements(versionElement))
	{
		if (conflictingElement == versionElement) continue;
		if (!versionPackageEntryPtr->isModificationAllowed(conflictingElement)) continue;

		if (__makes_sense_to_modify_package(solution, conflictingElement, brokenElement))
		{
			// other version seems to be ok
			unique_ptr< Action > action(new Action);
			action->oldElementPtr = versionElement;
			action->newElementPtr = conflictingElement;

			actions.push_back(std::move(action));
		}
	}
}

void NativeResolverImpl::__add_actions_to_fix_dependency(vector< unique_ptr< Action > >& actions,
		const PreparedSolution& solution, dg::Element brokenElement)
{
	// install one of versions package needs
	for (auto successorElement: __solution_storage->getSuccessorElements(brokenElement))
	{
		dg::Element conflictingElement;
		if (__solution_storage->simulateSetPackageEntry(solution, successorElement, &conflictingElement))
		{
			unique_ptr< Action > action(new Action);
			action->oldElementPtr = conflictingElement;
			action->newElementPtr = successorElement;

			actions.push_back(std::move(action));
		}
	}
}

void NativeResolverImpl::__prepare_reject_requests(vector< unique_ptr< Action > >& actions) const
{
	if (actions.size() <= 1) return;

	auto allNewElements = std::make_shared< vector<dg::Element> >();
	allNewElements->reserve(actions.size());

	for (const auto& action: actions)
	{
		action->allActionNewElements = allNewElements;
		allNewElements->push_back(action->newElementPtr);
	}
}

void NativeResolverImpl::__fillSuggestedPackageReasons(const PreparedSolution& solution,
		Resolver::SuggestedPackage& suggestedPackage, dg::Element element) const
{
	static const shared_ptr< const Reason > autoRemovalReason(new AutoRemovalReason);

	auto fillReasonElements = [&suggestedPackage](const IntroducedBy&, dg::Element element)
	{
		auto versionVertex = static_cast< const dg::VersionVertex* >(element);
		suggestedPackage.reasonPackageNames.push_back(versionVertex->getPackageName());
	};

	auto packageEntryPtr = solution.getPackageEntry(element);
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
					introducedBy, element, std::cref(fillReasonElements));
		}
	}
}

Resolver::UserAnswer::Type NativeResolverImpl::__propose_solution(
		const PreparedSolution& solution, Resolver::CallbackType callback, bool trackReasons)
{
	// build "user-frienly" version of solution
	Resolver::Offer offer;
	Resolver::SuggestedPackages& suggestedPackages = offer.suggestedPackages;

	for (auto packageEntry: solution.getEntries())
	{
		auto elementPtr = packageEntry->element;

		auto vertex = dynamic_cast< const dg::VersionVertex* >(elementPtr);
		if (vertex)
		{
			const string& packageName = vertex->getPackageName();
			if (!vertex->version && !__old_packages.count(packageName))
			{
				continue;
			}

			Resolver::SuggestedPackage& suggestedPackage = suggestedPackages[packageName];
			suggestedPackage.version = vertex->version;

			if (trackReasons)
			{
				__fillSuggestedPackageReasons(solution, suggestedPackage, elementPtr);
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
	if (p_debugging)
	{
		__mydebug_wrapper(solution, "proposing this solution");
	}

	auto userAnswer = callback(offer);
	if (p_debugging)
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
	dg::Element versionElement;
	BrokenSuccessor brokenSuccessor;
};

void NativeResolverImpl::__generate_possible_actions(vector< unique_ptr< Action > >* possibleActionsPtr,
		const PreparedSolution& solution, const BrokenPair& bp)
{
	auto brokenElement = bp.brokenSuccessor.elementPtr;

	__add_actions_to_fix_dependency(*possibleActionsPtr, solution, brokenElement);
	__add_actions_to_modify_package_entry(*possibleActionsPtr, solution,
			bp.versionElement, brokenElement);

	for (auto& action: *possibleActionsPtr)
	{
		action->brokenElementPriority = bp.brokenSuccessor.priority;
	}
}

void NativeResolverImpl::__final_verify_solution(const PreparedSolution& solution)
{
	for (auto packageEntry: solution.getEntries())
	{
		auto element = packageEntry->element;
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
		const PreparedSolution& solution, const map< dg::Element, size_t >& failCounts)
{
	auto failValue = [&failCounts](dg::Element e) -> size_t
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

	BrokenPair result = { nullptr, solution.getMaxBrokenSuccessor(compareBrokenSuccessors) };

	if (result.brokenSuccessor.elementPtr)
	{
		for (auto reverseDependency: solutionStorage.getPredecessorElements(result.brokenSuccessor.elementPtr))
		{
			if (solution.getPackageEntry(reverseDependency))
			{
				if (!result.versionElement || (result.versionElement->id < reverseDependency->id))
				{
					result.versionElement = reverseDependency;
				}
			}
		}
		if (!result.versionElement)
		{
			fatal2i("__get_broken_pair: no existing in the solution predecessors for the broken successor '%s', solution '%zu'",
					result.brokenSuccessor.elementPtr->toString(), solution.id);
		}
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
	ourIntroducedBy.versionElementPtr = bp.versionElement;
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

static void increaseQualityAdjustment(ssize_t* qa)
{
	const float factor = 1.45f;

	*qa += 1;
	*qa *= factor;
}

bool NativeResolverImpl::resolve(Resolver::CallbackType callback)
{
	auto initialSolution = std::make_shared< PreparedSolution >();
	__solution_storage.reset(new SolutionStorage(*__config, *__cache));
	__solution_storage->prepareForResolving(*initialSolution, __old_packages, p_userRelationExpressions);

	auto& sqa = __score_manager.qualityAdjustment;
	sqa = __config->getInteger("cupt::resolver::score::quality-adjustment");

	Resolve2Result subresult;
	while ((subresult = p_resolve2(initialSolution, callback)) == Resolve2Result::HitSolutionTreeLimit)
	{
		if (p_debugging) debug2("hit solution tree limit, old quality adjustment '%zd'", sqa);
		increaseQualityAdjustment(&sqa);
		if (p_debugging) debug2("restarting with quality adjustment '%zd'", sqa);
	}

	return subresult == Resolve2Result::Yes;
}

auto NativeResolverImpl::p_resolve2(const shared_ptr<PreparedSolution>& initialSolution, Resolver::CallbackType callback) -> Resolve2Result
{
	auto solutionChooser = __select_solution_chooser(*__config);

	const bool trackReasons = __config->getBool("cupt::resolver::track-reasons");
	const size_t maxSolutionCount = __config->getInteger("cupt::resolver::max-solution-count");
	p_maxLeafCount = __config->getInteger("cupt::resolver::max-leaf-count");

	if (p_debugging) debug2("started resolving");

	__any_solution_was_found = false;
	__decision_fail_tree.clear();

	SolutionContainer solutions = { initialSolution };

	// for each package entry 'count' will contain the number of failures
	// during processing these packages
	map< dg::Element, size_t > failCounts;

	while (!solutions.empty())
	{
		vector< unique_ptr< Action > > possibleActions;

		auto currentSolution = __get_next_current_solution(solutions, *__solution_storage, solutionChooser);

		auto problemFound = [this, &failCounts, &possibleActions, &currentSolution]
		{
			auto bp = __get_broken_pair(*__solution_storage, *currentSolution, failCounts);
			if (!bp.versionElement) return false;

			if (p_debugging)
			{
				__mydebug_wrapper(*currentSolution, "problem (%zu:%zu): %s: %s",
						bp.brokenSuccessor.elementPtr->getTypePriority(), bp.brokenSuccessor.priority,
						bp.versionElement->toString(), bp.brokenSuccessor.elementPtr->toString());
			}
			__generate_possible_actions(&possibleActions, *currentSolution, bp);
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
				if (p_debugging)
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
				if (p_debugging)
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
					return Resolve2Result::Yes;
				case Resolver::UserAnswer::Abandon:
					// user has selected abandoning all further efforts
					return Resolve2Result::No;
				case Resolver::UserAnswer::Decline:
					; // caller hasn't accepted this solution, well, go next...
			}
		}
		else
		{
			__prepare_reject_requests(possibleActions);

			if (possibleActions.empty())
			{
				if (p_debugging)
				{
					__mydebug_wrapper(*currentSolution, "no solutions");
				}
			}
			else
			{
				if (solutions.size()+possibleActions.size() > maxSolutionCount)
				{
					return Resolve2Result::HitSolutionTreeLimit;
				}

				auto callback = [&solutions](const shared_ptr< Solution >& solution)
				{
					solutions.insert(solution);
				};
				__pre_apply_actions_to_solution_tree(callback, currentSolution, possibleActions);
			}
		}
	}
	if (!__any_solution_was_found)
	{
		// no solutions pending, we have a great fail
		fatal2(__("unable to resolve dependencies, because of:\n\n%s"),
				__decision_fail_tree.toString());
	}
	return Resolve2Result::No;
}

}
}

