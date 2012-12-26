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

struct PackageEntry
{
	bool sticked;
	bool autoremoved;
	forward_list< const dg::Element* > rejectedConflictors;
	IntroducedBy introducedBy;

	PackageEntry(bool sticked_ = false);
	PackageEntry(PackageEntry&&) = default;
	PackageEntry(const PackageEntry&) = default;

	PackageEntry& operator=(PackageEntry&&) = default;
	PackageEntry& operator=(const PackageEntry&) = default;

	bool isModificationAllowed(const dg::Element*) const;
};

class PackageEntryMap;
class BrokenSuccessorMap;

struct BrokenSuccessor
{
	const dg::Element* elementPtr;
	size_t priority;
};

class Solution
{
	friend class SolutionStorage;

	shared_ptr< const Solution > __parent;
	shared_ptr< const PackageEntryMap > __master_entries;
	shared_ptr< PackageEntryMap > __added_entries;
	BrokenSuccessorMap*  __broken_successors;
 public:
	struct Action
	{
		const dg::Element* oldElementPtr; // may be NULL
		const dg::Element* newElementPtr; // many not be NULL
		vector< const dg::Element* > elementsToReject;
		shared_ptr< const Reason > reason;
		ScoreChange profit;
		IntroducedBy introducedBy;
		size_t brokenElementPriority;
	};

	size_t id;
	size_t level;
	bool finished;
	ssize_t score;
	std::unique_ptr< const Action > pendingAction;

	Solution();
	Solution(const Solution&) = delete;
	Solution& operator=(const Solution&) = delete;
	~Solution();

	void prepare();
	vector< const dg::Element* > getElements() const;

	const vector< BrokenSuccessor >& getBrokenSuccessors() const;
	// result becomes invalid after any setPackageEntry
	const PackageEntry* getPackageEntry(const dg::Element*) const;
};

class SolutionStorage
{
	size_t __next_free_id;
	size_t __get_new_solution_id(const Solution& parent);

	dg::DependencyGraph __dependency_graph;

	void __update_broken_successors(Solution&,
			const dg::Element*, const dg::Element*, size_t priority);

	struct Change
	{
		const dg::Element* insertedElementPtr;
		size_t parentSolutionId;

		explicit Change(size_t);
	};
	vector< Change > __change_index;
	void __update_change_index(size_t, const dg::Element*, const PackageEntry&);
	size_t __getInsertPosition(size_t solutionId, const dg::Element*) const;
 public:
	SolutionStorage(const Config&, const Cache& cache);

	shared_ptr< Solution > cloneSolution(const shared_ptr< Solution >&);
	shared_ptr< Solution > fakeCloneSolution(const shared_ptr< Solution >&);

	void prepareForResolving(Solution&,
			const map< string, const BinaryVersion* >&,
			const map< string, dg::InitialPackageEntry >&);
	const dg::Element* getCorrespondingEmptyElement(const dg::Element*);
	const GraphCessorListType& getSuccessorElements(const dg::Element*) const;
	const GraphCessorListType& getPredecessorElements(const dg::Element*) const;
	bool verifyElement(const Solution&, const dg::Element*) const;

	// may include parameter itself
	static const forward_list< const dg::Element* >&
			getConflictingElements(const dg::Element*);
	bool simulateSetPackageEntry(const Solution& solution,
			const dg::Element*, const dg::Element**) const;
	void setRejection(Solution&, const dg::Element*, const dg::Element*);
	void setPackageEntry(Solution&, const dg::Element*,
			PackageEntry&&, const dg::Element*, size_t);
	void unfoldElement(const dg::Element*);

	void processReasonElements(const Solution&, map< const dg::Element*, size_t >&,
			const IntroducedBy&, const dg::Element*,
			const std::function< void (const IntroducedBy&, const dg::Element*) >&) const;
	pair< const dg::Element*, const dg::Element* > getDiversedElements(
			size_t leftSolutionId, size_t rightSolutionId) const;
};

}
}

#endif

