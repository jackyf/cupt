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
		const SolutionStorage& solutionStorage, const PreparedSolution& solution,
		const IntroducedBy& lastIntroducedBy)
{
	vector< Decision > result;

	std::stack< Decision > chainStack;

	chainStack.push(Decision { lastIntroducedBy, 0, NULL });
	while (!chainStack.empty())
	{
		auto item = chainStack.top();
		chainStack.pop();

		result.push_back(item);

		auto queueItem = [&chainStack, &item](
				const IntroducedBy& introducedBy,
				const dg::Element* insertedElementPtr)
		{
			if (!introducedBy.empty())
			{
				chainStack.push(Decision { introducedBy, item.level + 1, insertedElementPtr });
			}
		};

		solutionStorage.processReasonElements(solution,
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

static std::pair< const dg::Element*, const dg::Element* > getDiversedElements(
		const vector< const dg::Element* >& left, const vector< const dg::Element* >& right)
{
	auto leftIt = left.begin();
	auto rightIt = right.begin();

	// precondition: left and right are different solutions and therefore have
	// diversed elements before both leftInsertedElements.end() and
	// rightInsertedElements.end()
	while (*leftIt == *rightIt)
	{
		++leftIt;
		++rightIt;
	}
	return { *leftIt, *rightIt };
}

void DecisionFailTree::addFailedSolution(const SolutionStorage& solutionStorage,
		const PreparedSolution& solution, const IntroducedBy& lastIntroducedBy)
{
	FailItem failItem;
	failItem.insertedElements = solution.getInsertedElements();

	auto fillFailItemDecisions = [&]()
	{
		if (failItem.decisions.empty())
		{
			failItem.decisions = __get_decisions(solutionStorage, solution, lastIntroducedBy);
		}
	};

	bool willBeAdded = true;

	auto it = __fail_items.begin();
	while (it != __fail_items.end())
	{
		auto diversedElements = getDiversedElements(it->insertedElements, failItem.insertedElements);
		auto existingIsDominant = __is_dominant(*it, diversedElements.first);
		if (existingIsDominant)
		{
			willBeAdded = false;
			++it;
		}
		else
		{
			fillFailItemDecisions();
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
		fillFailItemDecisions();
		__fail_items.push_back(std::move(failItem));
	}
}

void DecisionFailTree::clear()
{
	__fail_items.clear();
}

}
}
