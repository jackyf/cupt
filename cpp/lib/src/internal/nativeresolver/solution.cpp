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
#include <boost/range/adaptors.hpp>
#include <boost/range/any_range.hpp>

#include <cupt/cache.hpp>
#include <cupt/cache/binarypackage.hpp>

#include <internal/nativeresolver/solution.hpp>
#include <internal/nativeresolver/dependencygraph.hpp>

namespace cupt {
namespace internal {

SolutionStorage::SolutionStorage(const Config& config, const Cache& cache)
	: __next_free_id(1), __dependency_graph(config, cache)
{}

const GraphCessorListType& SolutionStorage::getSuccessorElements(const dg::Element* elementPtr) const
{
	return __dependency_graph.getSuccessorsFromPointer(elementPtr);
}

const GraphCessorListType& SolutionStorage::getPredecessorElements(const dg::Element* elementPtr) const
{
	return __dependency_graph.getPredecessorsFromPointer(elementPtr);
}

namespace {

const forward_list< const dg::Element* >& getRelatedElements(
		const dg::Element* elementPtr)
{
	static const forward_list< const dg::Element* > nullList;
	auto relatedElementPtrsPtr = elementPtr->getRelatedElements();
	return relatedElementPtrsPtr? *relatedElementPtrsPtr : nullList;
}

typedef boost::any_range< const dg::Element*, boost::forward_traversal_tag, const dg::Element*, std::ptrdiff_t > ElementRange;
auto getConflictingElements(const dg::Element* input) -> ElementRange
{
	using namespace std::placeholders;

	// the adapter under requires default-constructible function object :(
	std::function< bool (const dg::Element*) > predicate = [input](const dg::Element* element) { return element != input; };

	return getRelatedElements(input) | boost::adaptors::filtered(predicate);
}

}

/*
bool SolutionStorage::simulateSetPackageEntry(const Solution& solution,
		const dg::Element* elementPtr, const dg::Element** conflictingElementPtrPtr) const
{
	const forward_list< const dg::Element* >& conflictingElementPtrs =
			getConflictingElements(elementPtr);
	FORIT(conflictingElementPtrIt, conflictingElementPtrs)
	{
		if (*conflictingElementPtrIt == elementPtr)
		{
			continue;
		}
		if (auto packageEntryPtr = solution.getPackageEntry(*conflictingElementPtrIt))
		{
			// there may be only one conflicting element in the solution
			*conflictingElementPtrPtr = *conflictingElementPtrIt;

			return (!packageEntryPtr->sticked && packageEntryPtr->isModificationAllowed(elementPtr));
		}
	}

	// no conflicting elements in this solution
	*conflictingElementPtrPtr = NULL;
	if (auto versionElement = dynamic_cast< const dg::VersionElement* >(elementPtr))
	{
		if (versionElement->version)
		{
			*conflictingElementPtrPtr = const_cast< dg::DependencyGraph& >
					(__dependency_graph).getCorrespondingEmptyElement(elementPtr);
		}
	}
	return true;
}
*/

void SolutionStorage::p_addActionsToFixDependency(PossibleActions* actions, const dg::Element* brokenElement) const
{
	// one of versions package needs
	for (auto successorElement: getSuccessorElements(brokenElement))
	{
		actions->push_back(successorElement);
	}
}

bool contains(const GraphCessorListType& elist, const dg::Element* element)
{
	return (std::find(elist.begin(), elist.end(), element) != elist.end());
}

bool SolutionStorage::p_makesSenseToModifyPackage(
		const dg::Element* candidateElement, const dg::Element* brokenElement, bool debugging)
{
	unfoldElement(candidateElement);

	const auto& successorElements = getSuccessorElements(candidateElement);
	for (auto successorElement: successorElements)
	{
		if (successorElement == brokenElement)
		{
			if (debugging)
			{
				debug2("not considering %s: it has the same problem", candidateElement->toString());
			}
			return false;
		}
	}

	// let's try even harder to find if this candidate is really appropriate for us
	auto brokenElementTypePriority = brokenElement->getTypePriority();
	const auto& brokenElementSuccessors = getSuccessorElements(brokenElement);
	for (auto successorElement: successorElements)
	{
		/* we check only successors with the same or bigger priority than
		   currently broken one */
		if (successorElement->getTypePriority() < brokenElementTypePriority)
		{
			continue;
		}
		/* if any of such successors gives us equal or less "space" in
		   terms of satisfying elements, the version won't be accepted as a
		   resolution */
		bool isMoreWide = false;
		for (auto element: getSuccessorElements(successorElement))
		{
			if (!contains(brokenElementSuccessors, element))
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
				debug2("not considering %s: it contains equal or less wide relation expression '%s'",
						candidateElement->toString(), successorElement->toString());
			}
			return false;
		}
	}

	return true;
}

void SolutionStorage::p_addActionsToModifyCausingVersion(
		PossibleActions* actions, Problem problem, bool debugging)
{
	for (auto conflictingElement: getConflictingElements(problem.versionElement))
	{
		if (p_makesSenseToModifyPackage(conflictingElement, problem.brokenElement, debugging))
		{
			actions->push_back(conflictingElement);
		}
	}
}

auto SolutionStorage::p_getPossibleActions(Problem problem) -> PossibleActions
{
	PossibleActions result;

	p_addActionsToFixDependency(&result, problem.brokenElement);
	// FIXME: detect real debugging flag
	p_addActionsToModifyCausingVersion(&result, problem, true);

	return result;
}

void SolutionStorage::p_detectNewProblems(Solution& solution,
		const dg::Element* newElementPtr,
		queue<Problem>* problemQueue)
{
	auto newProblemCallback = [&](Problem problem)
	{
		debug2("    new problem: %s", problem.toString());
		problemQueue->push(problem);

		if (newElementPtr != problem.versionElement)
		{
			solution.p_addElementsAndEdgeToUniverse(newElementPtr, problem.versionElement);
		}
		solution.p_addElementsAndEdgeToUniverse(problem.versionElement, problem.brokenElement);
	};

	//debug2("  checking direct dependencies of the new element");
	for (auto successor: getSuccessorElements(newElementPtr))
	{
		if (!verifyNoConflictingSuccessors(solution, successor))
		{
			newProblemCallback({ newElementPtr, successor });
		}
	}

	//debug2("  invalidating those which depend on the old element(s)");
	for (auto conflictor: getConflictingElements(newElementPtr))
	{
		if (!solution.p_isPresent(conflictor)) continue;

		for (auto predecessor: getPredecessorElements(conflictor))
		{
			if (verifyNoConflictingSuccessors(solution, predecessor)) continue;

			//debug2("    unsatisfied predecessor: %s", predecessor->toString());
			for (auto reverseDependency: getPredecessorElements(predecessor))
			{
				if (solution.p_isPresent(reverseDependency))
				{
					newProblemCallback({ reverseDependency, predecessor });
				}
			}
		}
	}
}

static bool isVersionElement(const dg::Element* elementPtr)
{
	return dynamic_cast< const dg::VersionElement* >(elementPtr);
}

static bool isRelationElement(const dg::Element* element)
{
	return dynamic_cast< const dg::RelationVertex* >(element);
}

void SolutionStorage::setPackageEntry(Solution& solution,
		const dg::Element* element, const dg::Element* reasonElement)
{
	if (solution.p_isPresent(element)) return;

	debug2("adding '%s' to universe because of '%s'", element->toString(), reasonElement->toString());
	__dependency_graph.unfoldElement(element);
	solution.p_addElementsAndEdgeToUniverse(reasonElement, element);

	// TODO: save space by adding one back-edges to present vertices
	bool conflictorsFound = false;
	for (auto conflictor: getConflictingElements(element))
	{
		if (solution.p_isPresent(conflictor))
		{
			conflictorsFound = true;
			solution.p_universe.addEdgeFromPointers(element, conflictor);
			solution.p_universe.addEdgeFromPointers(conflictor, element);
		}
	}
	if (!conflictorsFound)
	{
		// there were zero elements of this family in the solution, so we
		// should add an empty one (which was virtually present before calling
		// this setPackageEntry())

		// TODO: check --no-remove still works
		if (isVersionElement(element))
		{
			if (auto emptyVariant = getCorrespondingEmptyElement(element))
			{
				setPackageEntry(solution, emptyVariant, element);
			}
		}
	}
}

void SolutionStorage::prepareForResolving(Solution& initialSolution,
			const map< string, const BinaryVersion* >& oldPackages,
			const map< string, dg::InitialPackageEntry >& initialPackages,
			const vector< dg::UserRelationExpression >& userRelationExpressions)
{
	auto source = __dependency_graph.fill(oldPackages, initialPackages);
	/* User relation expressions must be processed before any unfoldElement() calls
	   to early override version checks (if needed) for all explicitly required versions. */
	for (const auto& userRelationExpression: userRelationExpressions)
	{
		__dependency_graph.addUserRelationExpression(userRelationExpression);
	}

	for (const auto& element: source)
	{
		__dependency_graph.unfoldElement(element);
		initialSolution.p_universe.addVertex(element);
	}

	debug2("starting expanding the universe");
	initialSolution.p_dependencyGraph = &__dependency_graph;
	p_expandUniverse(initialSolution);
	initialSolution.p_markAsSettled(source.back() /* user requests */);
	debug2("finished expanding the universe");
}

void SolutionStorage::p_expandUniverse(Solution& initialSolution)
{
	queue< Problem > problemQueue;
	set< Problem > processedProblems;

	{
		auto copiedInitialVertices = initialSolution.p_universe.getVertices();
		for (auto element: copiedInitialVertices)
		{
			debug2("adding initial element '%s'", element->toString());
			p_detectNewProblems(initialSolution, element, &problemQueue);
		}
	}

	debug2("starting unrolling the problem queue");
	while (!problemQueue.empty())
	{
		auto problem = problemQueue.front();
		problemQueue.pop();

		if (processedProblems.insert(problem).second) // not processed yet
		{
			debug2("processing the problem '%s'", problem.toString());
			for (auto actionElement: p_getPossibleActions(problem))
			{
				bool actionElementPresent = initialSolution.p_isPresent(actionElement);
				if (!actionElementPresent)
				{
					setPackageEntry(initialSolution, actionElement, problem.brokenElement);
					p_detectNewProblems(initialSolution, actionElement, &problemQueue);
				}
			}
		}
	}
}

bool SolutionStorage::verifyNoConflictingSuccessors(const Solution& solution, const dg::Element* element) const
{
	//debug2("    verifying '%s'", element->toString());
	auto&& successors = getSuccessorElements(element);

	auto isEmptyVersion = [&solution](const dg::Element* element)
	{
		if (auto versionSuccessor = dynamic_cast< const dg::VersionElement* >(element))
		{
			return !versionSuccessor->version;
		}
		return false;
	};
	auto areThereConflictors = [&successors, &solution](const dg::Element* successor)
	{
		for (auto conflictor: getConflictingElements(successor))
		{
			if (!solution.p_isPresent(conflictor)) continue;
			if (contains(successors, conflictor)) continue;
			return true; // really conflicts
		}
		return false;
	};
	for (auto successor: successors)
	{
		// TODO: at this point there shouldn't be yet vital edges: if (successor == element) continue;
		if (!solution.p_isPresent(successor) && !isEmptyVersion(successor)) continue;
		if (areThereConflictors(successor)) continue;
		return true;
	}
	return false;
}

const dg::Element* SolutionStorage::getCorrespondingEmptyElement(const dg::Element* elementPtr)
{
	return __dependency_graph.getCorrespondingEmptyElement(elementPtr);
}

void SolutionStorage::unfoldElement(const dg::Element* elementPtr)
{
	__dependency_graph.unfoldElement(elementPtr);
}

size_t SolutionStorage::__getInsertPosition(size_t solutionId, const dg::Element* elementPtr) const
{
	(void)solutionId;
	(void)elementPtr;
	/*
	while (solutionId != 0)
	{
		const auto& change = __change_index[solutionId];
		if (change.insertedElementPtr == elementPtr) return solutionId;
		solutionId = change.parentSolutionId;
	}
	*/

	return -1;
}

/*
void SolutionStorage::processReasonElements(
		const Solution& solution, map< const dg::Element*, size_t >& elementPositionCache,
		const IntroducedBy& introducedBy, const dg::Element* insertedElementPtr,
		const std::function< void (const IntroducedBy&, const dg::Element*) >& callback) const
{
	auto getElementPosition = [this, &solution, &elementPositionCache](const dg::Element* elementPtr)
	{
		auto& value = elementPositionCache[elementPtr];
		if (!value)
		{
			value = __getInsertPosition(solution.id, elementPtr);
		}
		return value;
	};

	{ // version
		if (auto packageEntryPtr = solution.getPackageEntry(introducedBy.versionElementPtr))
		{
			callback(packageEntryPtr->introducedBy, introducedBy.versionElementPtr);
		}
	}
	// dependants
	set< const dg::Element* > alreadyProcessedConflictors;
	for (const auto& successor: getSuccessorElements(introducedBy.brokenElementPtr))
	{
		const dg::Element* conflictingElementPtr;
		if (!simulateSetPackageEntry(solution, successor, &conflictingElementPtr))
		{
			// conflicting element is surely exists here
			if (alreadyProcessedConflictors.insert(conflictingElementPtr).second)
			{
				// not yet processed

				// verifying that conflicting element was added to a
				// solution earlier than currently processed item
				auto conflictingElementInsertedPosition = getElementPosition(conflictingElementPtr);
				if (conflictingElementInsertedPosition == size_t(-1))
				{
					// conflicting element was not a resolver decision, so it can't
					// have valid 'introducedBy' anyway
					continue;
				}
				if (getElementPosition(insertedElementPtr) <= conflictingElementInsertedPosition)
				{
					// it means conflicting element was inserted to a solution _after_
					// the current element, so it can't be a reason for it
					continue;
				}

				// verified, queueing now
				const IntroducedBy& candidateIntroducedBy =
						solution.getPackageEntry(conflictingElementPtr)->introducedBy;
				callback(candidateIntroducedBy, conflictingElementPtr);
			}
		}
	}
}

pair< const dg::Element*, const dg::Element* > SolutionStorage::getDiversedElements(
		size_t leftSolutionId, size_t rightSolutionId) const
{
	const auto* leftChangePtr = &__change_index[leftSolutionId];
	const auto* rightChangePtr = &__change_index[rightSolutionId];

	while (leftChangePtr->parentSolutionId != rightChangePtr->parentSolutionId)
	{
		if (leftChangePtr->parentSolutionId < rightChangePtr->parentSolutionId)
		{
			rightChangePtr = &__change_index[rightChangePtr->parentSolutionId];
		}
		else
		{
			leftChangePtr = &__change_index[leftChangePtr->parentSolutionId];
		}
	}

	return { leftChangePtr->insertedElementPtr, rightChangePtr->insertedElementPtr };
}
*/

Solution::Solution()
	: id(0), finished(false), score(0)
{}

Solution::~Solution()
{}

bool Solution::operator<(const Solution& other) const
{
	return p_universe.getVertices() < other.p_universe.getVertices();
}

void Solution::p_addElementsAndEdgeToUniverse(const dg::Element* from, const dg::Element* to)
{
	p_universe.addVertex(from);
	p_universe.addVertex(to);
	if (p_universe.getPredecessorsFromPointer(to).empty())
	{
		p_universe.addEdgeFromPointers(from, to);
	}
}

bool Solution::p_isPresent(const dg::Element* elementPtr) const
{
	auto it = p_universe.getVertices().find(elementPtr);
	return it != p_universe.getVertices().end();
}

template< typename T >
T deepCopy(const T& value)
{
	return T(value);
}

void Solution::p_markAsSettled(const dg::Element* element)
{
	debug2("    settling %s", element->toString());

	for (auto successor: p_dependencyGraph->getSuccessorsFromPointer(element))
	{
		if (!p_isPresent(successor)) continue;

		debug2("      marking '%s' as vital", successor->toString());
		for (auto predecessorOfVital: deepCopy(p_universe.getPredecessorsFromPointer(successor)))
		{
			p_universe.deleteEdgeFromPointers(predecessorOfVital, successor);
		}
		p_universe.addEdgeFromPointers(successor, successor);

		reasonEdges.push_back({ element, successor });
	}
	for (auto successor: deepCopy(p_universe.getSuccessorsFromPointer(element)))
	{
		debug2("      deleting virtual edge '%s' -> '%s'",
				element->toString(), successor->toString());
		p_universe.deleteEdgeFromPointers(element, successor);
	}

	for (auto predecessor: deepCopy(p_universe.getPredecessorsFromPointer(element)))
	{
		if (isVersionElement(predecessor)) continue;

		// mark as satisfied
		if (p_dependencyGraph->hasEdgeFromPointers(predecessor, element))
		{
			debug2("      deleting predecessor '%s'", predecessor->toString());
			p_universe.deleteVertex(predecessor);
		}
		else
		{
			debug2("      deleting virtual edge '%s' -> '%s'",
					predecessor->toString(), element->toString());
			p_universe.deleteEdgeFromPointers(predecessor, element);
		}
	}
}

// returns false if Solution becomes invalid as a result of dropping
bool Solution::p_dropElementChain(const dg::Element* element)
{
	queue< const dg::Element* > dropCandidates;
	dropCandidates.push(element);

	while (!dropCandidates.empty())
	{
		auto candidate = dropCandidates.front();
		debug2("    considering up-dropping %s", candidate->toString());
		dropCandidates.pop();

		if (isVersionElement(candidate))
		{
			for (auto predecessor: p_universe.getPredecessorsFromPointer(candidate))
			{
				if (!isVersionElement(predecessor))
				{
					dropCandidates.push(predecessor);
				}
			}
		}
		else // relation element
		{
			bool isVital = false;
			bool hasVersionSuccessors = false;
			for (auto successor: p_universe.getSuccessorsFromPointer(candidate))
			{
				if (!isRelationElement(successor))
				{
					if (p_dependencyGraph->hasEdgeFromPointers(candidate, successor))
					{
						debug2("      no, has successor '%s'", successor->toString());
						hasVersionSuccessors = true;
						break;
					}
				}
				else if (successor == candidate)
				{
					isVital = true;
				}
				else
				{
					fatal2i("nativeresolver: solution: p_dropElement: non-self edge relation edge '%s' -> '%s'",
							candidate->toString(), successor->toString());
				}
			}
			if (hasVersionSuccessors) continue;
			// we should drop it as failed
			if (isVital)
			{
				debug2("      it's vital");
				return false; // boom! one less solution to process
			}

			// dropping
			for (auto predecessor: p_dependencyGraph->getPredecessorsFromPointer(candidate))
			{
				if (!p_isPresent(predecessor)) continue;

				dropCandidates.push(predecessor);
			}
		}

		debug2("      yes");
		p_dropElementChainDown(candidate);
	}
	return true;
}

void Solution::p_dropElementChainDown(const dg::Element* element)
{
	auto isDownDropAllowed = [this](const dg::Element* element, const dg::Element* excludingElement)
	{
		for (auto predecessor: p_universe.getPredecessorsFromPointer(element))
		{
			if (predecessor == excludingElement) continue;
			if (isVersionElement(element) && isVersionElement(predecessor)) continue;
			if (isRelationElement(element) && !p_dependencyGraph->hasEdgeFromPointers(predecessor, element)) continue;

			debug2("        no, has other predecessor '%s'", predecessor->toString());
			return false;
		}
		debug2("        yes");
		return true;
	};

	queue< const dg::Element* > elementsToDrop;
	elementsToDrop.push(element);
	while (!elementsToDrop.empty())
	{
		auto elementToDrop = elementsToDrop.front();
		elementsToDrop.pop();

		for (auto successor: p_universe.getSuccessorsFromPointer(elementToDrop))
		{
			if (isVersionElement(elementToDrop) && isVersionElement(successor)) continue;

			debug2("      considering down-dropping %s", successor->toString());
			if (isDownDropAllowed(successor, elementToDrop))
			{
				elementsToDrop.push(successor);
			}
		}

		p_universe.deleteVertex(elementToDrop);
	}
}

bool Solution::p_dropConflictingElements(const dg::Element* element)
{
	for (auto elementToDrop: getConflictingElements(element))
	{
		if (!p_isPresent(elementToDrop)) continue;

		if (!p_dropElementChain(elementToDrop))
		{
			debug2("    fail");
			return false;
		}
	}
	return true;
}

bool Solution::p_dropAlreadyProcessedElements(const dg::Element* actionElement,
		const vector< const dg::Element* >& processedActionElements)
{
	for (auto processedActionElement: processedActionElements)
	{
		if (processedActionElement == actionElement) continue; // current one, NOP

		if (!p_dropElementChain(processedActionElement))
		{
			debug2("    fail");
			return false;
		}
	}
	return true;
}

const dg::Element* Solution::p_selectMostUpRelationElement() const
{
	typedef vector< const dg::Element* > ElementGroup;
	const dg::Element* result = nullptr;
	// TODO: adjust signature of topologicalSortOf to not have two "callbacks"
	// TODO: implement early cancellation in topologicalSortOf callback
	auto callback = [&result](const ElementGroup& group, bool closed)
	{
		debug2("p_selectMostUpVersionElement: group");
		for (auto e: group)
		{
			debug2("  (%c) %s", (closed?'c':'o'), e->toString());
		}
		if (result) return; // found already
		for (auto element: group)
		{
			if (isRelationElement(element))
			{
				result = element;
				break;
			}
		}
	};

	std::vector< ElementGroup > dummyGroups;
	p_universe.topologicalSortOfStronglyConnectedComponents
			< std::less<const ElementGroup*> >(callback, std::back_inserter(dummyGroups));

	if (!result)
	{
		fatal2i("nativeresolver: solution: p_selectMostUpVersionElement: null result");
	}
	return result;
}

vector< Solution > Solution::reduce() const
{
	debug2("reducing:");

	auto relationElement = p_selectMostUpRelationElement();
	debug2("  relation: %s", relationElement->toString());
	vector< Solution > result;
	vector< const dg::Element* > processedActionElements;
	for (auto actionElement: p_universe.getSuccessorsFromPointer(relationElement))
	{
		if (actionElement == relationElement) continue;
		if (!p_isPresent(actionElement)) continue;

		debug2("  candidate: %s", actionElement->toString());
		Solution forked = *this;

		processedActionElements.push_back(actionElement);

		forked.p_markAsSettled(actionElement);
		if (!forked.p_dropConflictingElements(actionElement)) continue;
		if (!forked.p_dropAlreadyProcessedElements(actionElement, processedActionElements)) continue;

		debug2("    success");
		forked.reasonEdges.push_back({ relationElement, actionElement });
		result.push_back(std::move(forked));
	}

	// return alive forked solutions
	return result;
}

vector< Solution > Solution::split() const
{
	debug2("splitting:");
	auto copyIsland = [this](Solution* newSolution, const vector< const dg::Element* >& island)
	{
		for (auto element: island)
		{
			newSolution->p_universe.addVertex(element);
			for (auto successor: p_universe.getSuccessorsFromPointer(element))
			{
				newSolution->p_addElementsAndEdgeToUniverse(element, successor);
			}
		}
	};

	vector< Solution > result;
	for (const auto& island: p_universe.getWeaklyConnectedComponents())
	{
		debug2("  island (%zu):", island.size());
		for (const auto& element: island)
		{
			if (!isVersionElement(element)) continue;
			debug2("    %s", element->toString());
		}

		Solution newSolution;
		newSolution.p_dependencyGraph = p_dependencyGraph;
		copyIsland(&newSolution, island);
		result.push_back(newSolution);
	}
	return result;
}

const dg::Element* Solution::getFinishedElement() const
{
	const auto& vertices = p_universe.getVertices();
	if (vertices.empty())
	{
		fatal2i("nativeresolver: solution: getFinishedElement: empty universe");
	}
	if (vertices.size() == 1)
	{
		return *vertices.begin();
	}
	else
	{
		return nullptr;
	}
}

string Solution::toString() const
{
	vector< string > strings;
	for (auto element: p_universe.getVertices())
	{
		strings.push_back(element->toString());
	}
	return string("(") + join(", ", strings) + string(")");
}

}
}

