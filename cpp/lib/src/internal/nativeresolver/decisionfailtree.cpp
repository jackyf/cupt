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

string DecisionFailTree::__decisions_to_string(
		const vector< Decision >& decisions)
{
	auto insertedElementPtrToString = [](const dg::Element* elementPtr) -> string
	{
		if (!elementPtr)
		{
			return __("no solutions"); // root
		}
		auto versionElement = dynamic_cast< const dg::VersionElement* >(elementPtr);
		if (!versionElement)
		{
			fatal2i("__fail_leaf_to_string: '%s' is not a version element",
					elementPtr->toString());
		}
		return versionElement->toLocalizedString();
	};

	string result;
	FORIT(it, decisions)
	{
		result.append(it->level * 2, ' ');
		result.append(it->introducedBy.getReason()->toString());
		result.append(" -> ");
		result.append(insertedElementPtrToString(it->insertedElementPtr));
		result.append("\n");
	}
	return result;
}

string DecisionFailTree::toString() const
{
	string result;
	FORIT(childIt, __fail_items)
	{
		result += __decisions_to_string(childIt->decisions);
		result += "\n";
	}
	return result;
}

vector< DecisionFailTree::Decision > DecisionFailTree::__get_decisions(
		const SolutionStorage& solutionStorage, const Solution& solution,
		const PackageEntry::IntroducedBy& lastIntroducedBy)
{
	vector< Decision > result;

	map< const dg::Element*, size_t > elementPositionCache;
	std::stack< Decision > chainStack;

	chainStack.push(Decision { lastIntroducedBy, 0, NULL });
	while (!chainStack.empty())
	{
		auto item = chainStack.top();
		chainStack.pop();

		result.push_back(item);

		auto queueItem = [&chainStack, &item](
				const PackageEntry::IntroducedBy& introducedBy,
				const dg::Element* insertedElementPtr)
		{
			if (!introducedBy.empty())
			{
				chainStack.push(Decision { introducedBy, item.level + 1, insertedElementPtr });
			}
		};

		solutionStorage.processReasonElements(solution, elementPositionCache,
				item.introducedBy, item.insertedElementPtr, std::cref(queueItem));
	}

	return std::move(result);
}

// fail item is dominant if a diversed element didn't cause final breakage
bool DecisionFailTree::__is_dominant(const FailItem& failItem, const dg::Element* diversedElementPtr)
{
	FORIT(it, failItem.decisions)
	{
		if (it->insertedElementPtr == diversedElementPtr)
		{
			return false;
		}
	}
	return true;
}

void DecisionFailTree::addFailedSolution(const SolutionStorage& solutionStorage,
		const Solution& solution, const PackageEntry::IntroducedBy& lastIntroducedBy)
{
	FailItem failItem;
	failItem.solutionId = solution.id;
	failItem.decisions = __get_decisions(solutionStorage, solution, lastIntroducedBy);
	bool willBeAdded = true;

	auto it = __fail_items.begin();
	while (it != __fail_items.end())
	{
		auto diversedElements = solutionStorage.getDiversedElements(it->solutionId, failItem.solutionId);
		auto existingIsDominant = __is_dominant(*it, diversedElements.first);
		if (existingIsDominant)
		{
			willBeAdded = false;
			++it;
		}
		else
		{
			if (__is_dominant(failItem, diversedElements.second))
			{
				it = __fail_items.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	if (willBeAdded)
	{
		__fail_items.push_back(std::move(failItem));
	}
}

void DecisionFailTree::clear()
{
	__fail_items.clear();
}

}
}
