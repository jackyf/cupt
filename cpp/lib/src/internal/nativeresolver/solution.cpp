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
#include <internal/nativeresolver/cowmap.tpp>

namespace cupt {
namespace internal {

using std::make_pair;

PackageEntry::PackageEntry()
	: sticked(false), autoremoved(false), level(0)
{}

bool PackageEntry::isModificationAllowed(dg::Element element) const
{
	auto findResult = std::find(rejectedConflictors.begin(), rejectedConflictors.end(), element);
	return (findResult == rejectedConflictors.end());
}

template < class data_t, class KeyGetter >
class VectorBasedMap
{
 public:
	typedef dg::Element key_t;
	typedef data_t value_type; // for set_union
	typedef vector< data_t > container_t;
	typedef typename container_t::iterator iterator_t;
	typedef typename container_t::const_iterator const_iterator_t;
	typedef KeyGetter key_getter_t;
 private:
	container_t __container;
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
	void shrinkToFit() { __container.shrink_to_fit(); }
	const_iterator_t begin() const { return __container.begin(); }
	const_iterator_t end() const { return __container.end(); }
	const_iterator_t lower_bound(const key_t& key) const
	{
		return std::lower_bound(begin(), end(), key, __comparator());
	}
	iterator_t lower_bound(const key_t& key)
	{
		return std::lower_bound(__container.begin(), __container.end(), key, __comparator());
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
		return __container.insert(position, std::move(data));
	}
	void push_back(const data_t& data)
	{
		__container.push_back(data);
	}
 public:
	mutable size_t forkedCount;

	VectorBasedMap()
		: forkedCount(0)
	{}
};

typedef shared_ptr< const PackageEntry > SPPE;

struct PackageEntryMapKeyGetter
{
	dg::Element operator()(const pair< dg::Element, SPPE >& data)
	{ return data.first; }
};
class PackageEntryMap: public VectorBasedMap< pair< dg::Element, SPPE >, PackageEntryMapKeyGetter >
{};

struct BrokenSuccessorMapKeyGetter
{
	dg::Element operator()(const BrokenSuccessor& data) { return data.elementPtr; }
};
class BrokenSuccessorMap: public VectorBasedMap< BrokenSuccessor, BrokenSuccessorMapKeyGetter >
{};

class UnpreparedSolution: public Solution
{
 public:
	shared_ptr< const PreparedSolution > p_parent;
	std::unique_ptr< const Action > p_pendingAction;

	ssize_t getScore() const
	{
		return p_parent->getScore() + score;
	}
	bool isFinished() const
	{
		return false;
	}
	size_t getLevel() const
	{
		return p_parent->getLevel() + 1;
	}

	shared_ptr< PreparedSolution > prepare() const;
};

SolutionStorage::SolutionStorage(const Config& config, const Cache& cache)
	: __next_free_id(1), __dependency_graph(config, cache)
{}

SolutionStorage::~SolutionStorage()
{}

size_t SolutionStorage::__get_new_solution_id()
{
	return __next_free_id++;
}

shared_ptr< Solution > SolutionStorage::fakeCloneSolution(const shared_ptr< PreparedSolution >& source)
{
	source->id = __get_new_solution_id();
	++source->level;
	return source;
}

shared_ptr< Solution > SolutionStorage::cloneSolution(const shared_ptr< PreparedSolution >& source)
{
	auto cloned = std::make_shared< UnpreparedSolution >();

	cloned->p_parent = source;
	cloned->id = __get_new_solution_id();

	// other parts should be done by calling prepare outside

	return cloned;
}

const GraphCessorListType& SolutionStorage::getSuccessorElements(dg::Element elementPtr) const
{
	return __dependency_graph.getSuccessors(elementPtr);
}

const GraphCessorListType& SolutionStorage::getPredecessorElements(dg::Element elementPtr) const
{
	return __dependency_graph.getPredecessors(elementPtr);
}

const vector<dg::Element>& SolutionStorage::getConflictingElements(dg::Element element)
{
	static const vector<dg::Element> nullList;
	auto relatedElementPtrsPtr = element->getRelatedElements();
	return relatedElementPtrsPtr? *relatedElementPtrsPtr : nullList;
}

bool SolutionStorage::simulateSetPackageEntry(const PreparedSolution& solution,
		dg::Element element, dg::Element* conflictingElementPtr) const
{
	if (auto familyPackageEntry = solution.getFamilyPackageEntry(element))
	{
		*conflictingElementPtr = familyPackageEntry->element;
		return (!familyPackageEntry->sticked && familyPackageEntry->isModificationAllowed(element));
	}

	// no conflicting elements in this solution
	*conflictingElementPtr = nullptr;
	if (auto versionElement = dynamic_cast<dg::VersionElement>(element))
	{
		if (versionElement->version)
		{
			*conflictingElementPtr = const_cast< dg::DependencyGraph& >
					(__dependency_graph).getCorrespondingEmptyElement(element);
		}
	}
	return true;
}

void SolutionStorage::setRejection(PreparedSolution& solution, dg::Element element)
{
	dg::Element conflictingElement;
	simulateSetPackageEntry(solution, element, &conflictingElement);
	if (!conflictingElement) return;

	auto conflictorPackageEntryPtr = solution.getPackageEntry(conflictingElement);

	PackageEntry packageEntry = (conflictorPackageEntryPtr ?
			PackageEntry(*conflictorPackageEntryPtr) : PackageEntry());

	packageEntry.rejectedConflictors.push_front(element);
	solution.setPackageEntry(conflictingElement, std::move(packageEntry));
}

void SolutionStorage::setEmpty(PreparedSolution& solution, dg::Element element)
{
	auto emptyElement = __dependency_graph.getCorrespondingEmptyElement(element);

	PackageEntry packageEntry;
	packageEntry.autoremoved = true;

	solution.setPackageEntry(emptyElement, std::move(packageEntry));
}

void SolutionStorage::p_updateBrokenSuccessors(PreparedSolution& solution,
		dg::Element oldElement, dg::Element newElement, size_t priority)
{
	auto& bss = solution.p_brokenSuccessors;
	auto adaptedGetBs = [&bss](dg::Element element)
	{
		auto result = bss.get<size_t>(element);
		if (result && !*result) result = nullptr;
		return result;
	};

	auto reverseDependencyExists = [this, &solution](dg::Element element)
	{
		for (auto reverseDependencyPtr: getPredecessorElements(element))
		{
			if (solution.getPackageEntry(reverseDependencyPtr))
			{
				return true;
			}
		}
		return false;
	};
	auto isPresent = [](const GraphCessorListType& container, dg::Element element)
	{
		return std::find(container.begin(), container.end(), element) != container.end();
	};

	static const GraphCessorListType nullList;

	const auto& successorsOfOld = oldElement ? getSuccessorElements(oldElement) : nullList;
	const auto& successorsOfNew = getSuccessorElements(newElement);
	// check direct dependencies of the old element
	for (auto successorPtr: successorsOfOld)
	{
		if (isPresent(successorsOfNew, successorPtr)) continue;

		if (adaptedGetBs(successorPtr))
		{
			if (!reverseDependencyExists(successorPtr))
			{
				bss.remove(successorPtr);
			}
		}
	}
	// check direct dependencies of the new element
	for (auto successorPtr: successorsOfNew)
	{
		if (isPresent(successorsOfOld, successorPtr)) continue;

		auto it = adaptedGetBs(successorPtr);
		if (!it)
		{
			if (!verifyElement(solution, successorPtr))
			{
				bss.add(successorPtr, priority);
			}
		}
		else
		{
			bss.add(successorPtr, std::max(*it, priority));
		}
	}

	const auto& predecessorsOfOld = oldElement ? getPredecessorElements(oldElement) : nullList;
	const auto& predecessorsOfNew = getPredecessorElements(newElement);
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
				bss.add(predecessorElementPtr, priority);
			}
		}
	}
	// validate those which depend on the new element
	for (auto predecessorElementPtr: predecessorsOfNew)
	{
		if (isPresent(predecessorsOfOld, predecessorElementPtr)) continue;

		if (adaptedGetBs(predecessorElementPtr))
		{
			bss.remove(predecessorElementPtr);
		}
	}
}

void PreparedSolution::setPackageEntry(dg::Element element, PackageEntry&& packageEntry)
{
	packageEntry.element = element;
	auto newData = std::make_shared< const PackageEntry >(std::move(packageEntry));
	p_entries.add(element->getFamilyKey(), std::move(newData));
}

void SolutionStorage::prepareForResolving(PreparedSolution& initialSolution,
			const map< string, const BinaryVersion* >& oldPackages,
			const vector< dg::UserRelationExpression >& userRelationExpressions)
{
	auto source = __dependency_graph.fill(oldPackages);
	/* User relation expressions must be processed before any unfoldElement() calls
	   to early override version checks (if needed) for all explicitly required versions. */
	for (const auto& userRelationExpression: userRelationExpressions)
	{
		__dependency_graph.addUserRelationExpression(userRelationExpression);
	}
	for (const auto& record: source)
	{
		__dependency_graph.unfoldElement(record.second->element);
	}

	auto comparator = [](const pair< dg::Element, SPPE >& left, const pair< dg::Element, SPPE >& right)
	{
		return left.first < right.first;
	};
	std::sort(source.begin(), source.end(), comparator);

	p_initialEntries.reset(new PackageEntryMap);
	p_initialEntries->init(std::move(source));

	initialSolution.p_entries.setInitialMap(p_initialEntries.get());
	static const BrokenSuccessorMap nullBrokenSuccessorMap = {};
	initialSolution.p_brokenSuccessors.setInitialMap(&nullBrokenSuccessorMap);
	for (const auto& entry: *p_initialEntries)
	{
		p_updateBrokenSuccessors(initialSolution, nullptr, entry.second->element, 1);
	}
}

bool SolutionStorage::verifyElement(const PreparedSolution& solution, dg::Element element) const
{
	for (auto successor: getSuccessorElements(element))
	{
		if (auto entry = solution.getFamilyPackageEntry(successor))
		{
			if (entry->element == successor) return true;
		}
		else
		{
			// check for non-present empty elements as they are virtually present
			if (auto versionSuccessor = dynamic_cast<dg::VersionElement>(successor))
			{
				if (!versionSuccessor->version) return true;
			}
		}
	}
	return false;
}

void SolutionStorage::unfoldElement(dg::Element element)
{
	__dependency_graph.unfoldElement(element);
}

void SolutionStorage::processReasonElements(const PreparedSolution& solution,
		const IntroducedBy& introducedBy, dg::Element insertedElement,
		const std::function< void (const IntroducedBy&, dg::Element) >& callback) const
{
	{ // version
		if (auto packageEntryPtr = solution.getPackageEntry(introducedBy.versionElementPtr))
		{
			callback(packageEntryPtr->introducedBy, introducedBy.versionElementPtr);
		}
	}
	// dependants
	set<dg::Element> alreadyProcessedConflictors;
	for (const auto& successor: getSuccessorElements(introducedBy.brokenElementPtr))
	{
		dg::Element conflictingElement;
		if (!simulateSetPackageEntry(solution, successor, &conflictingElement))
		{
			// conflicting element is surely exists here
			if (alreadyProcessedConflictors.insert(conflictingElement).second)
			{
				// not yet processed
				auto conflictorPackageEntryPtr = solution.getPackageEntry(conflictingElement);

				if (insertedElement)
				{
					// verifying that conflicting element was added to a
					// solution earlier than currently processed item
					auto conflictingElementInsertedPosition = conflictorPackageEntryPtr->level;
					if (conflictingElementInsertedPosition == 0)
					{
						// conflicting element was not a resolver decision, so it can't
						// have valid 'introducedBy' anyway
						continue;
					}
					if (solution.getPackageEntry(insertedElement)->level <= conflictingElementInsertedPosition)
					{
						// it means conflicting element was inserted to a solution _after_
						// the current element, so it can't be a reason for it
						continue;
					}
				}

				// verified, queueing now
				callback(conflictorPackageEntryPtr->introducedBy, conflictingElement);
			}
		}
	}
}

void SolutionStorage::assignAction(Solution& solution, unique_ptr< Solution::Action >&& action)
{
	if (auto prepared = dynamic_cast< PreparedSolution* >(&solution))
	{
		// apply immediately
		p_applyAction(*prepared, *action);
	}
	else
	{
		auto unprepared = static_cast< UnpreparedSolution* >(&solution);
		unprepared->p_pendingAction = std::move(action);
	}
}

static void setRejections(SolutionStorage& solutionStorage, PreparedSolution& solution, const Solution::Action& action)
{
	auto reject = [&](dg::Element element)
	{
		solutionStorage.setRejection(solution, element);
	};

	if (!action.allActionNewElements) return; // nothing to reject

	if (action.newElementPtr->getUnsatisfiedType() != dg::Unsatisfied::None)
	{
		// all
		for (auto element: *action.allActionNewElements)
		{
			reject(element);
		}
	}
	else
	{
		auto uselessToReject = [&action](dg::Element element)
		{
			return action.newElementPtr->getFamilyKey() == element->getFamilyKey();
		};

		for (auto element: *action.allActionNewElements)
		{
			if (element == action.newElementPtr) break;
			if (uselessToReject(element)) continue;

			reject(element);
		}
	}
}

void SolutionStorage::p_setPackageEntryFromAction(PreparedSolution& solution, const Solution::Action& action)
{
	PackageEntry packageEntry;
	packageEntry.sticked = (action.brokenElementPriority > 0);
	packageEntry.introducedBy = action.introducedBy;
	packageEntry.level = solution.level;
	solution.setPackageEntry(action.newElementPtr, std::move(packageEntry));
}

void SolutionStorage::p_applyAction(PreparedSolution& solution, const Solution::Action& action)
{
	setRejections(*this, solution, action);
	p_setPackageEntryFromAction(solution, action);
	solution.p_entries.shrinkToFit();

	__dependency_graph.unfoldElement(action.newElementPtr);

	p_updateBrokenSuccessors(solution,
			action.oldElementPtr, action.newElementPtr, action.brokenElementPriority+1);
	solution.p_brokenSuccessors.shrinkToFit();
}

shared_ptr< PreparedSolution > SolutionStorage::prepareSolution(const shared_ptr< Solution >& input)
{
	if (auto prepared = dynamic_pointer_cast< PreparedSolution >(input))
	{
		return prepared;
	}
	else
	{
		auto unprepared = static_pointer_cast< UnpreparedSolution >(input);
		auto converted = unprepared->prepare();
		p_applyAction(*converted, *unprepared->p_pendingAction);
		return converted;
	}
}


Solution::Solution()
	: id(0), score(0)
{}

Solution::~Solution()
{}


PreparedSolution::PreparedSolution()
	: level(0), finished(false)
{}

PreparedSolution::~PreparedSolution()
{}

shared_ptr< PreparedSolution > UnpreparedSolution::prepare() const
{
	if (!p_parent)
	{
		fatal2i("nativeresolver: solution: unprepared solution has no parent");
	}

	auto result = std::make_shared< PreparedSolution >();
	result->id = id;
	result->score = getScore();
	result->level = getLevel();
	result->initEntriesFromParent(*p_parent);

	return result;
}

void PreparedSolution::initEntriesFromParent(const PreparedSolution& parent)
{
	p_entries = parent.p_entries;
	p_brokenSuccessors = parent.p_brokenSuccessors;
}

vector<const PackageEntry*> PreparedSolution::getEntries() const
{
	return p_entries.getEntries<PackageEntry>();
}

vector<dg::Element> PreparedSolution::getInsertedElements() const
{
	vector<dg::Element> result(level, nullptr);

	p_entries.foreachModifiedEntry(
			[&result](const PackageEntryMap::value_type& data)
			{
				const auto& level = data.second->level;
				if (!level) return;
				result[level-1] = data.first;
			});

	return result;
}

BrokenSuccessor PreparedSolution::getMaxBrokenSuccessor(
		const std::function< bool (BrokenSuccessor, BrokenSuccessor) >& comp) const
{
	BrokenSuccessor result{ nullptr, 0 };

	foreachBrokenSuccessor(
			[&result, &comp](BrokenSuccessor bs)
			{
				if (!result.elementPtr || comp(result, bs))
				{
					result = bs;
				}
			});

	return result;
}

void PreparedSolution::foreachBrokenSuccessor(
		const std::function< void (BrokenSuccessor) >& callback) const
{
	p_brokenSuccessors.foreachModifiedEntry(
			[&callback](const BrokenSuccessor& bs)
			{
				if (!bs.priority) return;
				callback(bs);
			});
}

const PackageEntry* PreparedSolution::getFamilyPackageEntry(dg::Element element) const
{
	auto entryData = p_entries.get< shared_ptr<const PackageEntry> >(element->getFamilyKey());
	return entryData ? entryData->get() : nullptr;
}

const PackageEntry* PreparedSolution::getPackageEntry(dg::Element element) const
{
	auto packageEntry = getFamilyPackageEntry(element);
	if (packageEntry && packageEntry->element != element) return nullptr;
	return packageEntry;
}

}
}

