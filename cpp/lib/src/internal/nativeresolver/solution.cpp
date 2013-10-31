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
			bool notFound = (std::find(brokenElementSuccessors.begin(),
					brokenElementSuccessors.end(), element)
					== brokenElementSuccessors.end());

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

auto SolutionStorage::p_getPossibleActions(Solution& solution, Problem problem) -> PossibleActions
{
	PossibleActions result;

	p_addActionsToFixDependency(&result, problem.brokenElement);
	// FIXME: detect real debugging flag
	p_addActionsToModifyCausingVersion(&result, problem, true);

	for (auto element: result)
	{
		solution.p_addElementsAndEdgeToUniverse(problem.brokenElement, element);
	}

	return result;
}

void SolutionStorage::p_detectNewProblems(Solution& solution,
		const dg::Element* newElementPtr,
		const GraphCessorListType& predecessorsDifference,
		queue<Problem>* problemQueue)
{
	/*
	auto isPresent = [](const GraphCessorListType& container, const dg::Element* elementPtr)
	{
		return std::find(container.begin(), container.end(), elementPtr) != container.end();
	};*/
	auto newProblemCallback = [this, &solution, &newElementPtr](const dg::Element* brokenElementPtr)
	{
		solution.p_universe.addVertex(brokenElementPtr);
		solution.p_universe.addEdgeFromPointers(newElementPtr, brokenElementPtr);
	};

	// check direct dependencies of the new element
	for (auto successor: getSuccessorElements(newElementPtr))
	{
		if (!verifyNoConflictingSuccessors(solution, successor))
		{
			problemQueue->push({ newElementPtr, successor });
			newProblemCallback(successor);
		}
	}

	// invalidate those which depend on the old element
	for (auto predecessor: predecessorsDifference)
	{
		for (auto reverseDependencyPtr: getPredecessorElements(predecessor))
		{
			if (solution.isPresent(reverseDependencyPtr))
			{
				problemQueue->push({ reverseDependencyPtr, predecessor });
				newProblemCallback(predecessor);
			}
		}
	}
}

void SolutionStorage::p_postAddElementToUniverse(Solution& solution,
		const dg::Element* newElementPtr, queue< Problem >* problemQueue)
{
	typedef vector< const dg::Element* > ElementVector;
	ElementVector group;
	auto getGroupPredecessors = [this, &group]()
	{
		ElementVector result;
		for (auto elementPtr: group)
		{
			auto subResult = getPredecessorElements(elementPtr);
			std::sort(subResult.begin(), subResult.end());

			ElementVector newResult;
			if (elementPtr != group.front())
			{
				std::set_intersection(result.begin(), result.end(), subResult.begin(), subResult.end(),
						std::back_inserter(newResult));
				newResult.swap(result);
			}
			else
			{
				subResult.swap(result);
			}
		}
		return result;
	};

	for (auto elementPtr: getConflictingElements(newElementPtr))
	{
		if (!solution.isPresent(elementPtr)) continue;

		group.push_back(elementPtr);
	}
	auto predecessorsOfOld = getGroupPredecessors();
	group.push_back(newElementPtr);
	auto predecessorsOfNew = getGroupPredecessors();
	ElementVector predecessorsDifference;
	std::set_difference(predecessorsOfOld.begin(), predecessorsOfOld.end(), predecessorsOfNew.begin(), predecessorsOfNew.end(),
			std::back_inserter(predecessorsDifference));

	p_detectNewProblems(solution, newElementPtr, predecessorsDifference, problemQueue);
}

void SolutionStorage::setPackageEntry(Solution& solution,
		const dg::Element* elementPtr, const dg::Element* reasonBrokenElementPtr)
{
	__dependency_graph.unfoldElement(elementPtr);
	solution.p_addElementsAndEdgeToUniverse(reasonBrokenElementPtr, elementPtr);
	// TODO: save space by adding one back-edges to present vertices
	for (auto conflictor: getConflictingElements(elementPtr))
	{
		if (solution.isPresent(conflictor))
		{
			solution.p_universe.addEdgeFromPointers(elementPtr, conflictor);
			solution.p_universe.addEdgeFromPointers(conflictor, elementPtr);
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

	p_expandUniverse(initialSolution);
}

void SolutionStorage::p_expandUniverse(Solution& initialSolution)
{
	queue< Problem > problemQueue;
	set< Problem > processedProblems;

	for (const auto& element: initialSolution.p_universe.getVertices())
	{
		p_postAddElementToUniverse(initialSolution, element, &problemQueue);
	}

	while (!problemQueue.empty())
	{
		auto problem = problemQueue.front();
		problemQueue.pop();

		if (processedProblems.insert(problem).second) // not processed yet
		{
			for (auto actionElement: p_getPossibleActions(initialSolution, problem))
			{
				setPackageEntry(initialSolution, actionElement, problem.brokenElement);
				p_postAddElementToUniverse(initialSolution, actionElement, &problemQueue);
			}
		}
	}
}

bool SolutionStorage::verifyNoConflictingSuccessors(const Solution& solution, const dg::Element* element) const
{
	for (auto successor: getSuccessorElements(element))
	{
		if (successor == element) continue;

		for (auto conflictor: getConflictingElements(successor))
		{
			if (solution.isPresent(conflictor))
			{
				return false;
			}
		}
	}
	return true;
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
	p_universe.addEdgeFromPointers(from, to);
}

bool Solution::isPresent(const dg::Element* elementPtr) const
{
	auto it = p_universe.getVertices().find(elementPtr);
	return it != p_universe.getVertices().end();
}

bool isVersionElement(const dg::Element* elementPtr)
{
	return dynamic_cast< const dg::VersionElement* >(elementPtr);
}

vector< Solution > Solution::reduce() const
{
	const dg::Element* firstVersionElement;
	for (auto element: p_universe.getVertices())
	{
		if (isVersionElement(element))
		{
			firstVersionElement = element;
			break;
		}
	}

	vector< Solution > result;
	for (auto familyElement: getRelatedElements(firstVersionElement))
	{
		Solution forked = *this;
		// mark successors of chosen element as vital (self-edge)
		for (auto successor: p_universe.getSuccessorsFromPointer(familyElement))
		{
			if (!isVersionElement(successor)) continue; // broken element
			// ...
		}
		// drop non-chosen elements in each forked solution, at first attempt to drop vital vertices, drop the whole solution
		// else add forked to result
	}

	// return alive forked solutions
	return result;
}

vector< Solution > Solution::split() const
{
	auto copyIsland = [this](Solution* newSolution, const vector< const dg::Element* >& island)
	{
		for (auto element: island)
		{
			for (auto successor: p_universe.getSuccessorsFromPointer(element))
			{
				newSolution->p_addElementsAndEdgeToUniverse(element, successor);
			}
		}
	};

	vector< Solution > result;
	for (const auto& island: p_universe.getWeaklyConnectedComponents())
	{
		debug2("island (%zu):", island.size());
		for (const auto& element: island)
		{
			if (!isVersionElement(element)) continue;
			debug2("  %s", element->toString());
		}

		Solution newSolution;
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

}
}

