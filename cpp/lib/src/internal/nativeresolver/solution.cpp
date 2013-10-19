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

#include <cupt/cache.hpp>
#include <cupt/cache/binarypackage.hpp>

#include <internal/nativeresolver/solution.hpp>
#include <internal/nativeresolver/dependencygraph.hpp>

namespace cupt {
namespace internal {

using std::make_pair;

PackageEntry::PackageEntry(bool sticked_)
	: sticked(sticked_), autoremoved(false)
{}

bool PackageEntry::isModificationAllowed(const dg::Element* elementPtr) const
{
	auto findResult = std::find(rejectedConflictors.begin(),
			rejectedConflictors.end(), elementPtr);
	return (findResult == rejectedConflictors.end());
}

template < class data_t, class KeyGetter >
class VectorBasedMap
{
 public:
	typedef const dg::Element* key_t;
	typedef data_t value_type; // for set_union
	typedef vector< data_t > container_t;
	typedef data_t* iterator_t;
	typedef const data_t* const_iterator_t;
 private:
	container_t __container;
	typename container_t::iterator __position_to_iterator(const_iterator_t position)
	{
		return static_cast< typename container_t::iterator >(const_cast< iterator_t >(position));
	}
	struct __comparator
	{
		bool operator()(const data_t& data, const key_t& key) const
		{
			return KeyGetter()(data) < key;
		}
	};
 public:
	void init(container_t&& container) { __container.swap(container); }
	size_t size() const { return __container.size(); }
	void reserve(size_t size) { __container.reserve(size); }
	const_iterator_t begin() const { return &*__container.begin(); }
	const_iterator_t end() const { return &*__container.end(); }
	const_iterator_t lower_bound(const key_t& key) const
	{
		return std::lower_bound(begin(), end(), key, __comparator());
	}
	iterator_t lower_bound(const key_t& key)
	{
		return const_cast< iterator_t >(((const VectorBasedMap*)this)->lower_bound(key));
	}
	const_iterator_t find(const key_t& key) const
	{
		auto result = lower_bound(key);
		if (result != end() && KeyGetter()(*result) != key)
		{
			result = end();
		}
		return result;
	}
	// this insert() is called only for unexisting elements
	iterator_t insert(const_iterator_t position, data_t&& data)
	{
		auto distance = position - begin();
		__container.insert(__position_to_iterator(position), std::move(data));
		return const_cast< iterator_t >(begin()) + distance;
	}
	void erase(const_iterator_t position)
	{
		__container.erase(__position_to_iterator(position));
	}
	void push_back(const data_t& data)
	{
		__container.push_back(data);
	}
	const container_t& getContainer() const
	{
		return __container;
	}
};

typedef shared_ptr< const PackageEntry > SPPE;

struct PackageEntryMapKeyGetter
{
	const dg::Element* operator()(const pair< const dg::Element*, SPPE >& data)
	{ return data.first; }
};
class PackageEntryMap: public VectorBasedMap<
		pair< const dg::Element*, SPPE >, PackageEntryMapKeyGetter >
{
 public:
	mutable size_t forkedCount;

	PackageEntryMap()
		: forkedCount(0)
	{}
};

struct BrokenSuccessorMapKeyGetter
{
	const dg::Element* operator()(const BrokenSuccessor& data) { return data.elementPtr; }
};
class BrokenSuccessorMap: public VectorBasedMap< BrokenSuccessor, BrokenSuccessorMapKeyGetter >
{};

SolutionStorage::Change::Change(size_t parentSolutionId_)
	: parentSolutionId(parentSolutionId_)
{}

SolutionStorage::SolutionStorage(const Config& config, const Cache& cache)
	: __next_free_id(1), __dependency_graph(config, cache)
{}

size_t SolutionStorage::__get_new_solution_id(const Solution& parent)
{
	__change_index.emplace_back(parent.id);
	return __next_free_id++;
}

shared_ptr< Solution > SolutionStorage::fakeCloneSolution(const shared_ptr< Solution >& source)
{
	source->id = __get_new_solution_id(*source);
	return source;
}

shared_ptr< Solution > SolutionStorage::cloneSolution(const shared_ptr< Solution >& source)
{
	auto cloned = std::make_shared< Solution >();
	cloned->score = source->score;
	cloned->level = source->level + 1;
	cloned->id = __get_new_solution_id(*source);
	cloned->finished = false;

	cloned->__parent = source;

	// other part should be done by calling prepare outside

	return cloned;
}

const GraphCessorListType& SolutionStorage::getSuccessorElements(const dg::Element* elementPtr) const
{
	return __dependency_graph.getSuccessorsFromPointer(elementPtr);
}

const GraphCessorListType& SolutionStorage::getPredecessorElements(const dg::Element* elementPtr) const
{
	return __dependency_graph.getPredecessorsFromPointer(elementPtr);
}

const forward_list< const dg::Element* >& SolutionStorage::getConflictingElements(
		const dg::Element* elementPtr)
{
	static const forward_list< const dg::Element* > nullList;
	auto relatedElementPtrsPtr = elementPtr->getRelatedElements();
	return relatedElementPtrsPtr? *relatedElementPtrsPtr : nullList;
}

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

void SolutionStorage::p_addActionsToFixDependency(PossibleActions* actions,
		const Solution& solution, const dg::Element* brokenElement)
{
	// one of versions package needs
	for (auto successorElement: getSuccessorElements(brokenElement))
	{
		actions->push_back(successorElement);
	}
}

bool SolutionStorage::p_makesSenseToModifyPackage(const Solution& solution,
		const dg::Element* candidateElement, const dg::Element* brokenElement, bool debugging)
{
	unfoldElement(candidateElement);

	const auto& successorElements = getSuccessorElements(candidateElement);
	for (successorElement: successorElements)
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
	auto brokenElementTypePriority = brokenElementPtr->getTypePriority();
	const auto& brokenElementSuccessors = getSuccessorElements(brokenElementPtr);
	for (successorElement: successorElements)
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

void NativeResolverImpl::p_addActionsToModifyCausingVersion(
		PossibleActions* actions, const Solution& solution, Problem problem, bool debugging)
{
	auto versionElement = problem.versionElement;

	auto versionPackageEntryPtr = solution.getPackageEntry(versionElementPtr);

	for (conflictingElement: getConflictingElements(versionElement))
	{
		if (conflictingElement == versionElementPtr)
		{
			continue;
		}
		if (p_makesSenseToModifyPackage(solution, conflictingElement, problem.brokenElement, debugging))
		{
			actions->push_back(conflictingElement);
		}
	}
}

auto SolutionStorage::p_getPossibleActions(Solution& solution, Problem problem) -> PossibleActions
{
	PossibleActions result;

	// FIXME: implement
	// FIXME: detect real debugging flag
	p_addActionsToFixDependency(&result, solution, problem.brokenElement);
	p_addActionsToModifyCausingVersion(&result, solution, problem, true);
}

void SolutionStorage::p_detectNewProblems(Solution& solution,
		const dg::Element* newElementPtr,
		const GraphCessorListType& predecessorsDifference,
		queue<Problem>* problemQueue)
{
	auto isPresent = [](const GraphCessorListType& container, const dg::Element* elementPtr)
	{
		return std::find(container.begin(), container.end(), elementPtr) != container.end();
	};
	auto newProblemCallback = [this, &solution, &newElementPtr](const dg::Element* brokenElementPtr)
	{
		solution.p_universe.addVertex(brokenElementPtr);
		solution.p_universe.addEdgeFromPointers(newElementPtr, brokenElementPtr);
	};

	// check direct dependencies of the new element
	for (auto successor: getSuccessorElements(newElementPtr))
	{
		if (!verifyElement(solution, successor))
		{
			problemQueue->push({ newElementPtr, successor });
			newProblemCallback(successor);
		}
	}

	// invalidate those which depend on the old element
	for (auto predecessor: predecessorsDifference)
	{
		if (isPresent(successorsOfNew, predecessor)) continue;

		for (auto reverseDependencyPtr: getPredecessorElements(predecessor))
		{
			if (solution.getPackageEntry(reverseDependencyPtr))
			{
				problemQueue->push({ reverseDependencyPtr, brokenElementPtr });
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
		if (elementPtr == newElementPtr) continue;
		if (!solution.getPackageEntry(elementPtr)) continue;

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

	solution.p_universe.addVertex(reasonBrokenElementPtr);
	solution.p_universe.addVertex(elementPtr);
	solution.p_universe.addEdgeFromPointers(reasonBrokenElementPtr, elementPtr);
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
	for (const auto& record: source)
	{
		__dependency_graph.unfoldElement(record.first);
	}

	auto comparator = [](const pair< const dg::Element*, SPPE >& left,
			const pair< const dg::Element*, SPPE >& right)
	{
		return left.first < right.first;
	};
	std::sort(source.begin(), source.end(), comparator);

	initialSolution.p_universe->init(std::move(source));
	p_expandUniverse(initialSolution);
}

void SolutionStorage::p_expandUniverse(Solution& initialSolution)
{
	queue< Problem > problemQueue;
	set< Problem > processedProblems;

	for (const auto& entry: *initialSolution.p_universe)
	{
		p_postAddElementToUniverse(initialSolution, entry.first, &problemQueue);
	}

	while (!problemQueue.empty())
	{
		auto problem = problemQueue.top();
		problemQueue.pop();

		if (processedProblems.insert(problem).second) // not processed yet
		{
			for (const auto& actionElement: p_getPossibleActions(problem))
			{
				setPackageEntry(initialSolution, actionElement);
				p_postAddElementToUniverse(initialSolution, actionElement, &problemQueue);
			}
		}
	}
}

bool SolutionStorage::verifyElement(const Solution& solution,
		const dg::Element* elementPtr) const
{
	const GraphCessorListType& successorElementPtrs =
			getSuccessorElements(elementPtr);
	FORIT(elementPtrIt, successorElementPtrs)
	{
		if (solution.getPackageEntry(*elementPtrIt))
		{
			return true;
		}
	}

	// second try, check for non-present empty elements as they are virtually present
	FORIT(elementPtrIt, successorElementPtrs)
	{
		if (auto versionElement = dynamic_cast< const dg::VersionElement* >(*elementPtrIt))
		{
			if (!versionElement->version)
			{
				const dg::Element* conflictorPtr;
				if (simulateSetPackageEntry(solution, versionElement, &conflictorPtr), !conflictorPtr)
				{
					return true;
				}
			}
		}
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
	while (solutionId != 0)
	{
		const auto& change = __change_index[solutionId];
		if (change.insertedElementPtr == elementPtr) return solutionId;
		solutionId = change.parentSolutionId;
	}

	return -1;
}

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

Solution::Solution()
	: id(0), level(0), finished(false), score(0)
{
	p_entries.reset(new PackageEntryMap);
	__broken_successors = new BrokenSuccessorMap;
}

Solution::~Solution()
{
	delete __broken_successors;
}

vector< const dg::Element* > Solution::giveStickedElements()
{
	// FIXME: implement
	return {};
}

void Solution::prepare()
{
	if (!__parent) return; // prepared already

	__parent.reset();
}

vector< const dg::Element* > Solution::getElements() const
{
	// FIXME: return reference and modify callers accordingly
	vector< const dg::Element* > result;
	for (const auto& data: *p_entries)
	{
		result.push_back(data.first);
	}
	return result;
}

const PackageEntry* Solution::getPackageEntry(const dg::Element* elementPtr) const
{
	auto it = p_entries->find(elementPtr);
	if (it != p_entries->end())
	{
		return it->second.get();
	}
	return nullptr; // not found
}

void SolutionStorage::debugIslands(Solution& solution)
{
	auto& graph = solution.p_brokenElementSplitGraph;

	auto isInitialElement = [&solution](const dg::Element* elementPtr)
	{
		if (auto pe = solution.getPackageEntry(elementPtr))
		{
			const auto& ib = pe->introducedBy;
			return ib.empty();
		}
		return false;
	};
	auto findPresentElement = [this, &isInitialElement, &graph](const dg::Element* elementPtr)
	{
		for (auto conflictingElementPtr: getConflictingElements(elementPtr))
		{
			if (isInitialElement(conflictingElementPtr))
			{
				return conflictingElementPtr;
			}
		}
		return graph.addVertex(getCorrespondingEmptyElement(elementPtr));
	};
	auto isVersionElement = [](const dg::Element* elementPtr) -> bool
	{
		return dynamic_cast< const dg::VersionElement* >(elementPtr);
	};

	for (const auto& elementPtr: graph.getVertices())
	{
		if (!isVersionElement(elementPtr)) continue;

		auto presentElementPtr = findPresentElement(elementPtr);
		if (presentElementPtr == elementPtr) continue;

		debug2("merging %s to %s", elementPtr->toString(), presentElementPtr->toString());

		for (auto predecessor: getPredecessorElements(elementPtr))
		{
			graph.addEdgeFromPointers(predecessor, presentElementPtr);
		}
		for (auto successor: getSuccessorElements(elementPtr))
		{
			graph.addEdgeFromPointers(presentElementPtr, successor);
		}
	}
	for (const auto& island: graph.getWeaklyConnectedComponents())
	{
		if (island.size() > 1)
		{
			debug2("island:");
			set< const dg::Element* > seen;
			for (const auto& elementPtr: island)
			{
				if (!isVersionElement(elementPtr)) continue;
				if (!isInitialElement(elementPtr)) continue;

				auto presentElementPtr = findPresentElement(elementPtr);
				if (seen.insert(presentElementPtr).second)
				{
					debug2("  %s", presentElementPtr->toString());
				}
			}
		}
	}
}

}
}

