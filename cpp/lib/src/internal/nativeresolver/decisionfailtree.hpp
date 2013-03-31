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
#ifndef CUPT_INTERNAL_NATIVERESOLVER_DECISIONFAILTREE_SEEN
#define CUPT_INTERNAL_NATIVERESOLVER_DECISIONFAILTREE_SEEN

#include <list>

#include <internal/nativeresolver/solution.hpp>
#include <internal/graph.hpp>

namespace cupt {
namespace internal {

using std::unique_ptr;

class DecisionFailTree
{
	struct Decision
	{
		IntroducedBy introducedBy;
		size_t level;
		const dg::Element* insertedElementPtr;
	};
	struct FailItem
	{
		size_t solutionId;
		vector< Decision > decisions;
	};
	std::list< FailItem > __fail_items;

	static string __decisions_to_string(const vector< Decision >&);
	static vector< Decision > __get_decisions(
			const SolutionStorage& solutionStorage, const Solution& solution, const IntroducedBy&);
	static bool __is_dominant(const FailItem&, const dg::Element*);
 public:
	string toString() const;
	void addFailedSolution(const SolutionStorage&, const Solution&, const IntroducedBy&);
	void clear();
};

}
}

#endif

