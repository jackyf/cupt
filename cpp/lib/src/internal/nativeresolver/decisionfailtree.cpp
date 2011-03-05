/**************************************************************************
*   Copyright (C) 2011 by Eugene V. Lyubimkin                             *
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
#include <stack>

#include <internal/nativeresolver/decisionfailtree.hpp>

namespace cupt {
namespace internal {

string DecisionFailTree::__fail_leaf_to_string(const __fail_leaf_t& failLeaf)
{
	auto insertedElementPtrToString = [](const dg::Element* elementPtr)
	{
		if (!elementPtr)
		{
			return __("no solutions"); // root
		}
		auto versionElement = dynamic_cast< const dg::VersionElement >(*elementPtr);
		if (!versionElement)
		{
			fatal("internal error: __fail_leaf_to_string: '%s' is not a version element",
					(*elementPtr)->toString().c_str());
		}
		return versionElement->toLocalizedString();
	};

	string result;
	FORIT(it, failLeaf)
	{
		result.append(it->level * 2, ' ');
		auto mainPart = (*it->introducedBy.brokenElementPtr)->
				getReason(**it->introducedBy.versionElementPtr)->toString();
		result.append(std::move(mainPart));
		result.append(" -> ");
		result.append(insertedElementPtrToString(it->insertedElementPtr));
		result.append("\n");
	}
	return result;
}

string DecisionFailTree::toString() const
{
	if (__children.empty())
	{
		// this should be a leaf node
		if (!__fail_leaf_ptr)
		{
			fatal("internal error: no fail information in the leaf node");
		}
		return __fail_leaf_to_string(*__fail_leaf_ptr);
	}
	else
	{
		vector< string > parts;
		FORIT(childIt, __children)
		{
			parts.push_back((*childIt)->toString());
		}
		return join("\n", parts);
	}
}

unique_ptr< DecisionFailTree::__fail_leaf_t > DecisionFailTree::__get_fail_leaf(
		const SolutionStorage& solutionStorage, const Solution& solution,
		const PackageEntry::IntroducedBy& lastIntroducedBy)
{
	unique_ptr< DecisionFailTree::__fail_leaf_t > result(
			new DecisionFailTree::__fail_leaf_t);

	std::stack< Decision > chainStack;
	auto queueItem = [&chainStack](
			const PackageEntry::IntroducedBy& introducedBy, size_t level,
			const dg::Element* insertedElementPtr)
	{
		if (!introducedBy.empty())
		{
			chainStack.push(Decision { introducedBy, level, insertedElementPtr });
		}
	};

	queueItem(lastIntroducedBy, 0, NULL);
	while (!chainStack.empty())
	{
		auto item = chainStack.top();
		chainStack.pop();

		result->push_back(item);

		const PackageEntry::IntroducedBy& introducedBy = item.introducedBy;

		// processing subchains
		{ // version
			if (auto packageEntryPtr = solution.getPackageEntry(introducedBy.versionElementPtr))
			{
				queueItem(packageEntryPtr->introducedBy,
						item.level + 1, introducedBy.versionElementPtr);
			}
		}
		// dependants
		if (!solutionStorage.verifyElement(solution, introducedBy.brokenElementPtr))
		{
			set< const dg::Element* > alreadyProcessedConflictors;
			const list< const dg::Element* >& successors =
					solutionStorage.getSuccessorElements(introducedBy.brokenElementPtr);
			FORIT(successorIt, successors)
			{
				const dg::Element* conflictingElementPtr;
				if (!solutionStorage.simulateSetPackageEntry(
						solution, *successorIt, &conflictingElementPtr))
				{
					// conflicting element is surely exists here
					if (alreadyProcessedConflictors.insert(conflictingElementPtr).second)
					{
						// not yet processed
						const PackageEntry::IntroducedBy& candidateIntroducedBy =
								solution.getPackageEntry(conflictingElementPtr)->introducedBy;
						queueItem(candidateIntroducedBy, item.level + 1, conflictingElementPtr);
					}
				}
			}
		}
	}

	return std::move(result);
}

// fail leaf is dominant if a diversed element didn't cause final breakage
bool DecisionFailTree::__is_fail_leaf_dominant(
		const __fail_leaf_t& failLeaf, const dg::Element* diversedElementPtr)
{
	FORIT(it, failLeaf)
	{
		if (it->introducedBy.versionElementPtr == diversedElementPtr)
		{
			return false;
		}
	}
	return true;
}

void DecisionFailTree::__insert_fail_leaf(
		unique_ptr< const __fail_leaf_t >&& failLeafPtr, bool isFirstDominant,
		vector< const dg::Element* >::const_iterator insertedElementsIt,
		vector< const dg::Element* >::const_iterator insertedElementsEnd)
{
	__is_dominant = isFirstDominant;
	__inserted_element_ptr = *insertedElementsIt;

	++insertedElementsIt;
	if (insertedElementsIt != insertedElementsEnd)
	{
		__children.push_front(unique_ptr< DecisionFailTree >(new DecisionFailTree));
		(*__children.begin())->__insert_fail_leaf(std::move(failLeafPtr),
				__is_fail_leaf_dominant(*failLeafPtr, *insertedElementsIt),
				insertedElementsIt, insertedElementsEnd);
	}
	else
	{
		__fail_leaf_ptr = std::move(failLeafPtr);
	}
}

void DecisionFailTree::addFailedSolution(const SolutionStorage& solutionStorage,
		const Solution& solution, const PackageEntry::IntroducedBy& lastIntroducedBy)
{
	// first, find the diverse point
	auto insertedElementsIt = solution.insertedElementPtrs.begin();
	DecisionFailTree* currentTree = this;
	{
		diverse_point_cycle:
		FORIT(childIt, currentTree->__children)
		{
			if ((*childIt)->__inserted_element_ptr == *insertedElementsIt)
			{
				currentTree = childIt->get();
				++insertedElementsIt;
				goto diverse_point_cycle;
			}
		}
	}

	auto failLeafPtr = __get_fail_leaf(solutionStorage, solution, lastIntroducedBy);
	bool isDominant = __is_fail_leaf_dominant(*failLeafPtr, *insertedElementsIt);

	bool noDominantsPresent = true;
	FORIT(childIt, currentTree->__children)
	{
		if ((*childIt)->__is_dominant)
		{
			noDominantsPresent = false;
			break;
		}
	}

	if (noDominantsPresent) // dominants are "equal" between
	{
		if (isDominant)
		{
			currentTree->__children.clear(); // eat recessives
		}
		currentTree->__insert_fail_leaf(std::move(failLeafPtr), isDominant,
				insertedElementsIt, solution.insertedElementPtrs.end());
	}
}

void DecisionFailTree::clear()
{
	__fail_leaf_ptr.reset();
	__children.clear();
}

}
}
