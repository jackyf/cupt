/**************************************************************************
*   Copyright (C) 2010 by Eugene V. Lyubimkin                             *
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
#ifndef CUPT_INTERNAL_SOLUTION_SEEN
#define CUPT_INTERNAL_SOLUTION_SEEN

#include <bitset>
#include <map>
#include <forward_list>
#include <cstring>

#include <cupt/cache/binaryversion.hpp>
#include <cupt/system/resolver.hpp>

#include <internal/nativeresolver/score.hpp>
#include <internal/nativeresolver/dependencygraph.hpp>

namespace cupt {
namespace internal {

namespace dg = dependencygraph;
typedef dg::DependencyGraph::CessorListType GraphCessorListType;

using namespace cache;
using namespace system;

using std::map;
using std::forward_list;

struct IntroducedBy
{
	const dg::Element* versionElementPtr;
	const dg::Element* brokenElementPtr;

	IntroducedBy() : versionElementPtr(NULL) {}
	bool empty() const { return !versionElementPtr; }
	bool operator<(const IntroducedBy& other) const
	{
		return std::memcmp(this, &other, sizeof(*this)) < 0;
	}
	shared_ptr< const Resolver::Reason > getReason() const
	{
		return brokenElementPtr->getReason(*versionElementPtr);
	}
};

class Solution
{
	friend class SolutionStorage;

	Graph< const dg::Element* > p_universe;

	void p_addElementsAndEdgeToUniverse(const dg::Element*, const dg::Element*);
	bool p_isPresent(const dg::Element*) const;
	void p_markAsSettled(const dg::Element*);
	bool p_dropElement(const dg::Element*);
	bool p_dropConflictingElements(const dg::Element*);
	const dg::Element* p_selectMostUpVersionElement() const;
 public:
	struct Action
	{
		const dg::Element* oldElementPtr; // may be NULL
		const dg::Element* newElementPtr; // many not be NULL
		shared_ptr< const Reason > reason;
		ScoreChange profit;
		IntroducedBy introducedBy;
		size_t brokenElementPriority;
	};

	struct ReasonEdge
	{
		const dg::Element* from;
		const dg::Element* to;
	};

	size_t id;
	bool finished;
	ssize_t score;
	vector< ReasonEdge > reasonEdges;

	Solution();
	~Solution();

	string toString() const;

	vector< Solution > split() const;
	vector< Solution > reduce() const;
	const dg::Element* getFinishedElement() const;

	// sorting
	bool operator<(const Solution&) const;
};

class SolutionStorage
{
	size_t __next_free_id;

	dg::DependencyGraph __dependency_graph;

	struct Problem
	{
		const dg::Element* versionElement;
		const dg::Element* brokenElement;

		bool operator<(const Problem& other) const
		{
			return std::make_tuple(versionElement, brokenElement) <
					std::make_tuple(other.versionElement, other.brokenElement);
		}
		string toString() const
		{
			return brokenElement->getReason(*versionElement)->toString();
		}
	};
	void p_detectNewProblems(Solution& solution, const dg::Element*, queue<Problem>*);
	void p_expandUniverse(Solution&);

	typedef vector< const dg::Element* > PossibleActions;
	void p_addActionsToFixDependency(PossibleActions*, const dg::Element*) const;
	bool p_makesSenseToModifyPackage(const dg::Element*, const dg::Element*, bool debugging);
	void p_addActionsToModifyCausingVersion(PossibleActions* actions, Problem problem, bool debugging);
	PossibleActions p_getPossibleActions(Problem);

	size_t __getInsertPosition(size_t solutionId, const dg::Element*) const;
 public:
	SolutionStorage(const Config&, const Cache& cache);

	shared_ptr< Solution > cloneSolution(const shared_ptr< Solution >&);
	shared_ptr< Solution > fakeCloneSolution(const shared_ptr< Solution >&);

	void prepareForResolving(Solution&,
			const map< string, const BinaryVersion* >&,
			const map< string, dg::InitialPackageEntry >&,
			const vector< dg::UserRelationExpression >&);
	const dg::Element* getCorrespondingEmptyElement(const dg::Element*);
	const GraphCessorListType& getSuccessorElements(const dg::Element*) const;
	const GraphCessorListType& getPredecessorElements(const dg::Element*) const;
	bool verifyNoConflictingSuccessors(const Solution&, const dg::Element*) const;

	// may include parameter itself
	bool simulateSetPackageEntry(const Solution& solution,
			const dg::Element*, const dg::Element**) const;
	void setPackageEntry(Solution&, const dg::Element*, const dg::Element*);
	void unfoldElement(const dg::Element*);

	void processReasonElements(const Solution&, map< const dg::Element*, size_t >&,
			const IntroducedBy&, const dg::Element*,
			const std::function< void (const IntroducedBy&, const dg::Element*) >&) const;
	pair< const dg::Element*, const dg::Element* > getDiversedElements(
			size_t leftSolutionId, size_t rightSolutionId) const;
	void debugIslands(Solution&);
};

}
}

#endif

