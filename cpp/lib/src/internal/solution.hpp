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

#include <cupt/cache/binaryversion.hpp>
#include <cupt/system/resolver.hpp>

#include <internal/graph.hpp>
#include <internal/copyptr.hpp>

namespace cupt {
namespace internal {

using namespace cache;
using namespace system;

using std::bitset;

const size_t relationTypesCount = BinaryVersion::RelationTypes::Count;
typedef bitset< relationTypesCount > RelationTypesBitset;

struct PackageEntry
{
	shared_ptr< const BinaryVersion > version;
	bool sticked;
	CopyPtr< RelationLine > fakelySatisfied;
	CopyPtr< vector< shared_ptr< const Resolver::Reason > > > reasons;
	RelationTypesBitset checked;

	PackageEntry();
};

typedef map< string, PackageEntry > PackageEntryMap;

struct DependencyEntry
{
	BinaryVersion::RelationTypes::Type type;
	bool isAnti;
};

class Solution
{
	friend class SolutionStorage;

	shared_ptr< const Solution > __parent;
	shared_ptr< PackageEntryMap > __master_package_entries;
	shared_ptr< PackageEntryMap > __package_entries;

 public:
	size_t id;
	size_t level;
	float score;
	bool finished;
	shared_ptr< const void > pendingAction;

	Solution();

	void prepare();
	vector< string > getPackageNames() const;
	vector< const string* > getMostlyUncheckedPackageNames(BinaryVersion::RelationTypes::Type) const;
	const PackageEntry* getPackageEntry(const string& packageName) const;
	PackageEntry* setPackageEntryIfExists(const string& packageName);
};

class SolutionStorage
{
	shared_ptr< const Cache > __cache;
	string __dummy_package_name;
	Graph< string > __dependency_graph;
	vector< DependencyEntry > __dependency_entries;
	size_t __next_free_id;

	void __add_package_dependencies(const string& packageName);
 public:
	SolutionStorage(const shared_ptr< const Cache >&,
			const vector< DependencyEntry >&, const string& dummyPackageName);
	// should be called only with versions with the same package name
	void addVersionDependencies(const vector< shared_ptr< const BinaryVersion > >&);
	shared_ptr< Solution > cloneSolution(const shared_ptr< Solution >&);
	PackageEntry* setPackageEntry(Solution&, const string& packageName);
	void __invalidate(Solution&, const string& packageName, PackageEntry*);
};

}
}

#endif

