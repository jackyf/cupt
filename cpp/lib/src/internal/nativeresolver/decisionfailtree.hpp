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

#include <forward_list>

#include <internal/nativeresolver/solution.hpp>
#include <internal/graph.hpp>

namespace cupt {
namespace internal {

using std::unique_ptr;

class DecisionFailTree
{
	struct Decision
	{
		PackageEntry::IntroducedBy introducedBy;
		size_t level;
		const dg::Element* insertedElementPtr;
	};
	typedef vector< Decision > __fail_leaf_t;
	forward_list< unique_ptr< DecisionFailTree > > __children;
	const dg::Element* __inserted_element_ptr;
	unique_ptr< const __fail_leaf_t > __fail_leaf_ptr; // may be NULL
	bool __is_dominant;

	static string __fail_leaf_to_string(const __fail_leaf_t&);
	static unique_ptr< DecisionFailTree::__fail_leaf_t > __get_fail_leaf(
			const SolutionStorage& solutionStorage, const Solution& solution,
			const PackageEntry::IntroducedBy&);
	static bool __is_fail_leaf_dominant(const __fail_leaf_t&, const dg::Element*);
	void __insert_fail_leaf(unique_ptr< const __fail_leaf_t >&&, bool,
			vector< const dg::Element* >::const_iterator,
			vector< const dg::Element* >::const_iterator);
 public:
	string toString() const;
	void addFailedSolution(const SolutionStorage&, const Solution&,
			const PackageEntry::IntroducedBy&);
	void clear();
};

}
}

#endif

