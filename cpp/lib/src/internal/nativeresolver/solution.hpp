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

#include <map>
#include <forward_list>
#include <cstring>

#include <cupt/cache/binaryversion.hpp>
#include <cupt/system/resolver.hpp>

#include <internal/nativeresolver/score.hpp>
#include <internal/nativeresolver/dependencygraph.hpp>
#include <internal/nativeresolver/cowmap.hpp>

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
	dg::Element versionElementPtr;
	dg::Element brokenElementPtr;

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
	dg::Element element;
	bool sticked;
	bool autoremoved;
	forward_list<dg::Element> rejectedConflictors;
	IntroducedBy introducedBy;
	size_t level;

	PackageEntry();
	PackageEntry(PackageEntry&&) = default;
	PackageEntry(const PackageEntry&) = default;

	PackageEntry& operator=(PackageEntry&&) = default;
	PackageEntry& operator=(const PackageEntry&) = default;

	bool isModificationAllowed(dg::Element) const;
};

class PackageEntryMap;
class BrokenSuccessorMap;

struct BrokenSuccessor
{
	dg::Element elementPtr;
	size_t priority;
};

class Solution
{
 public:
	struct Action
	{
		dg::Element oldElementPtr; // may be NULL
		dg::Element newElementPtr; // many not be NULL
		shared_ptr< vector<dg::Element> > allActionNewElements;
		IntroducedBy introducedBy;
		size_t brokenElementPriority;
	};

	size_t id;
	ssize_t score;

	Solution();
	Solution(const Solution&) = delete;
	Solution& operator=(const Solution&) = delete;
	virtual ~Solution();

	virtual ssize_t getScore() const = 0;
	virtual bool isFinished() const = 0;
	virtual size_t getLevel() const = 0;
};

class PreparedSolution: public Solution
{
	friend class SolutionStorage;

	CowMap< dg::Element, PackageEntryMap > p_entries;
	CowMap< dg::Element, BrokenSuccessorMap > p_brokenSuccessors;

	const PackageEntry* getFamilyPackageEntry(dg::Element) const;
	void setPackageEntry(dg::Element, PackageEntry&&);
 public:
	PreparedSolution();
	~PreparedSolution();
	void initEntriesFromParent(const PreparedSolution&);

	size_t level;
	bool finished;

	ssize_t getScore() const { return score; }
	bool isFinished() const { return finished; }
	size_t getLevel() const { return level; }

	vector< const PackageEntry* > getEntries() const;
	vector<dg::Element> getInsertedElements() const;

	void foreachBrokenSuccessor(const std::function< void (BrokenSuccessor) >& callback) const;
	BrokenSuccessor getMaxBrokenSuccessor(const std::function< bool (BrokenSuccessor, BrokenSuccessor) >&) const;

	const PackageEntry* getPackageEntry(dg::Element) const;
};

class SolutionStorage
{
	size_t __next_free_id;
	size_t __get_new_solution_id();

	dg::DependencyGraph __dependency_graph;
	unique_ptr< PackageEntryMap > p_initialEntries;

	void p_updateBrokenSuccessors(PreparedSolution&,
			dg::Element, dg::Element, size_t priority);
	inline void p_setPackageEntryFromAction(PreparedSolution&, const Solution::Action&);
	void p_applyAction(PreparedSolution&, const Solution::Action&);
 public:
	SolutionStorage(const Config&, const Cache& cache);
	~SolutionStorage();

	shared_ptr< Solution > cloneSolution(const shared_ptr< PreparedSolution >&);
	shared_ptr< Solution > fakeCloneSolution(const shared_ptr< PreparedSolution >&);

	void prepareForResolving(PreparedSolution&,
			const map< string, const BinaryVersion* >&,
			const vector< dg::UserRelationExpression >&);

	void assignAction(Solution& solution, unique_ptr< Solution::Action >&& action);
	shared_ptr< PreparedSolution > prepareSolution(const shared_ptr< Solution >&);

	const GraphCessorListType& getSuccessorElements(dg::Element) const;
	const GraphCessorListType& getPredecessorElements(dg::Element) const;
	bool verifyElement(const PreparedSolution&, dg::Element) const;

	// may include parameter itself
	static const forward_list<dg::Element>& getConflictingElements(dg::Element);
	bool simulateSetPackageEntry(const PreparedSolution&, dg::Element, dg::Element*) const;
	void setRejection(PreparedSolution&, dg::Element);
	void setEmpty(PreparedSolution&, dg::Element);
	void unfoldElement(dg::Element);

	void processReasonElements(const PreparedSolution&, const IntroducedBy&, dg::Element,
			const std::function< void (const IntroducedBy&, dg::Element) >&) const;
};

}
}

#endif

