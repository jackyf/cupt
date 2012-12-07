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
	cloned->level = source->level;
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

void SolutionStorage::setRejection(Solution& solution,
		const dg::Element* elementPtr, const dg::Element* dontChangePtr)
{
	const dg::Element* conflictingElementPtr;
	simulateSetPackageEntry(solution, elementPtr, &conflictingElementPtr);
	if (!conflictingElementPtr || conflictingElementPtr == dontChangePtr)
	{
		return;
	}
	auto conflictorPackageEntryPtr = solution.getPackageEntry(conflictingElementPtr);

	PackageEntry packageEntry = (conflictorPackageEntryPtr ?
			PackageEntry(*conflictorPackageEntryPtr) : PackageEntry());

	packageEntry.rejectedConflictors.push_front(elementPtr);
	setPackageEntry(solution, conflictingElementPtr,
			std::move(packageEntry), NULL, -1);
}

void SolutionStorage::__update_broken_successors(Solution& solution,
		const dg::Element* oldElementPtr, const dg::Element* newElementPtr, size_t priority)
{
	if (priority == (size_t)-1)
	{
		return;
	}

	auto& bss = *solution.__broken_successors;

	auto reverseDependencyExists = [this, &solution](const dg::Element* elementPtr)
	{
		for (auto reverseDependencyPtr: getPredecessorElements(elementPtr))
		{
			if (solution.getPackageEntry(reverseDependencyPtr))
			{
				return true;
			}
		}
		return false;
	};
	auto isPresent = [](const GraphCessorListType& container, const dg::Element* elementPtr)
	{
		return std::find(container.begin(), container.end(), elementPtr) != container.end();
	};

	static const GraphCessorListType nullList;

	const auto& successorsOfOld = oldElementPtr ? getSuccessorElements(oldElementPtr) : nullList;
	const auto& successorsOfNew = getSuccessorElements(newElementPtr);
	// check direct dependencies of the old element
	for (auto successorPtr: successorsOfOld)
	{
		if (isPresent(successorsOfNew, successorPtr)) continue;

		auto it = bss.find(successorPtr);
		if (it != bss.end())
		{
			if (!reverseDependencyExists(successorPtr))
			{
				bss.erase(it);
			}
		}
	}
	// check direct dependencies of the new element
	for (auto successorPtr: successorsOfNew)
	{
		if (isPresent(successorsOfOld, successorPtr)) continue;

		auto it = bss.lower_bound(successorPtr);
		if (it == bss.end() || it->elementPtr != successorPtr)
		{
			if (!verifyElement(solution, successorPtr))
			{
				bss.insert(it, BrokenSuccessor { successorPtr, priority });
			}
		}
		else
		{
			it->priority = std::max(it->priority, priority);
		}
	}

	const auto& predecessorsOfOld = oldElementPtr ? getPredecessorElements(oldElementPtr) : nullList;
	const auto& predecessorsOfNew = getPredecessorElements(newElementPtr);
	// invalidate those which depend on the old element
	for (auto predecessorElementPtr: predecessorsOfOld)
	{
		if (isPresent(predecessorsOfNew, predecessorElementPtr)) continue;
		if (isPresent(successorsOfNew, predecessorElementPtr)) continue;

		if (reverseDependencyExists(predecessorElementPtr))
		{
			if (!verifyElement(solution, predecessorElementPtr))
			{
				// here we assume brokenSuccessors didn't
				// contain predecessorElementPtr, since as old element was
				// present, predecessorElementPtr was not broken
				bss.insert(bss.lower_bound(predecessorElementPtr),
						BrokenSuccessor { predecessorElementPtr, priority });
			}
		}
	}
	// validate those which depend on the new element
	for (auto predecessorElementPtr: predecessorsOfNew)
	{
		if (isPresent(predecessorsOfOld, predecessorElementPtr)) continue;

		auto it = bss.find(predecessorElementPtr);
		if (it != bss.end())
		{
			bss.erase(it);
		}
	}
}

void SolutionStorage::__update_change_index(size_t solutionId,
		const dg::Element* newElementPtr, const PackageEntry& packageEntry)
{
	if (!packageEntry.sticked)
	{
		return; // not a "main" change
	}

	__change_index[solutionId].insertedElementPtr = newElementPtr;
}

void SolutionStorage::setPackageEntry(Solution& solution,
		const dg::Element* elementPtr, PackageEntry&& packageEntry,
		const dg::Element* conflictingElementPtr, size_t priority)
{
	__dependency_graph.unfoldElement(elementPtr);
	__update_change_index(solution.id, elementPtr, packageEntry);

	auto it = solution.__added_entries->lower_bound(elementPtr);
	if (it == solution.__added_entries->end() || it->first != elementPtr)
	{
		// there is no modifiable element in this solution
		solution.__added_entries->insert(it,
				make_pair(elementPtr, std::make_shared< const PackageEntry >(std::move(packageEntry))));
	}
	else
	{
		if (conflictingElementPtr && it->second)
		{
			fatal2i("conflicting elements in __added_entries: solution '%u', in '%s', out '%s'",
					solution.id, elementPtr->toString(), conflictingElementPtr->toString());
		}
		it->second = std::make_shared< const PackageEntry >(std::move(packageEntry));
	}

	if (conflictingElementPtr)
	{
		auto forRemovalIt = solution.__added_entries->lower_bound(conflictingElementPtr);
		if (forRemovalIt != solution.__added_entries->end() && forRemovalIt->first == conflictingElementPtr)
		{
			forRemovalIt->second.reset();
		}
		else
		{
			solution.__added_entries->insert(forRemovalIt, { conflictingElementPtr, {} });
		}
	}

	__update_broken_successors(solution, conflictingElementPtr, elementPtr, priority);
}

void SolutionStorage::prepareForResolving(Solution& initialSolution,
			const map< string, const BinaryVersion* >& oldPackages,
			const map< string, dg::InitialPackageEntry >& initialPackages)
{
	auto source = __dependency_graph.fill(oldPackages, initialPackages);
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

	initialSolution.__added_entries->init(std::move(source));
	for (const auto& entry: *initialSolution.__added_entries)
	{
		__update_broken_successors(initialSolution, NULL, entry.first, 0);
	}

	__change_index.emplace_back(0);
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
		const PackageEntry::IntroducedBy& introducedBy, const dg::Element* insertedElementPtr,
		const std::function< void (const PackageEntry::IntroducedBy&, const dg::Element*) >& callback) const
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
				const PackageEntry::IntroducedBy& candidateIntroducedBy =
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
	__added_entries.reset(new PackageEntryMap);
	__broken_successors = new BrokenSuccessorMap;
}

Solution::~Solution()
{
	delete __broken_successors;
}

template < typename CallbackType >
void __foreach_solution_element(const PackageEntryMap& masterEntries, const PackageEntryMap& addedEntries,
		CallbackType callback)
{
	class RepackInsertIterator: public std::iterator< std::output_iterator_tag, PackageEntryMap::value_type >
	{
		const CallbackType& __callback;
	 public:
		RepackInsertIterator(const CallbackType& callback_)
			: __callback(callback_) {}
		RepackInsertIterator& operator++() { return *this; }
		RepackInsertIterator& operator*() { return *this; }
		void operator=(const PackageEntryMap::value_type& data)
		{
			if (data.second)
			{
				__callback(data);
			}
		}
	};
	struct Comparator
	{
		bool operator()(const PackageEntryMap::value_type& left, const PackageEntryMap::value_type& right)
		{ return left.first < right.first; }
	};
	// it's important that parent's __added_entries come first,
	// if two elements are present in both (i.e. an element is overriden)
	// the new version of an element will be considered
	std::set_union(addedEntries.begin(), addedEntries.end(),
			masterEntries.begin(), masterEntries.end(),
			RepackInsertIterator(callback), Comparator());
}

void Solution::prepare()
{
	if (!__parent)
	{
		return; // prepared already
	}

	if (!__parent->__master_entries)
	{
		// parent solution is a master solution, build a slave on top of it
		__master_entries = __parent->__added_entries;
	}
	else
	{
		// this a slave solution
		size_t& forkedCount = __parent->__master_entries->forkedCount;
		forkedCount += __parent->__added_entries->size();
		if (forkedCount > __parent->__master_entries->size())
		{
			forkedCount = 0;

			// master solution is overdiverted, build new master one
			__added_entries->reserve(__parent->__added_entries->size() +
					__parent->__master_entries->size());

			__foreach_solution_element(*__parent->__master_entries, *__parent->__added_entries,
					[this](const PackageEntryMap::value_type& data) { this->__added_entries->push_back(data); });
		}
		else
		{
			// build new slave solution from current
			__master_entries = __parent->__master_entries;
			*__added_entries = *(__parent->__added_entries);
		}
	}

	*__broken_successors = *__parent->__broken_successors;
	__parent.reset();
}

vector< const dg::Element* > Solution::getElements() const
{
	vector< const dg::Element* > result;

	static const PackageEntryMap nullPackageEntryMap;
	const auto& masterEntries = __master_entries ? *__master_entries : nullPackageEntryMap;

	__foreach_solution_element(masterEntries, *__added_entries,
			[&result](const PackageEntryMap::value_type& data) { result.push_back(data.first); });

	return result;
}

const vector< BrokenSuccessor >& Solution::getBrokenSuccessors() const
{
	return __broken_successors->getContainer();
}

const PackageEntry* Solution::getPackageEntry(const dg::Element* elementPtr) const
{
	auto it = __added_entries->find(elementPtr);
	if (it != __added_entries->end())
	{
		return it->second.get();
	}
	if (__master_entries)
	{
		it = __master_entries->find(elementPtr);
		if (it != __master_entries->end())
		{
			return it->second.get();
		}
	}

	return NULL; // not found
}

}
}

