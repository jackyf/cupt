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
#include <set>

#include <cupt/cache/binaryversion.hpp>
#include <cupt/system/resolver.hpp>

#include <internal/copyptr.hpp>

namespace cupt {
namespace internal {

using namespace cache;
using namespace system;

using std::bitset;
using std::map;
using std::set;

typedef BinaryVersion::RelationTypes::Type RelationType;
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

class PackageEntryMap;

struct DependencyEntry
{
	RelationType type;
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
	std::unique_ptr< const void > pendingAction;

	Solution();

	void prepare();
	vector< string > getPackageNames() const;
	vector< string > getUncheckedPackageNames(RelationType) const;
	bool getPackageEntry(const string& packageName, PackageEntry*) const;
	void validate(const string& packageName,
			const PackageEntry&, RelationType);
};

class SolutionStorage
{
	struct PackageDependency
	{
		string packageName;
		RelationType relationType;
		bool operator<(const PackageDependency& other) const
		{
			auto packageNameComparisonResult = packageName.compare(other.packageName);
			if (packageNameComparisonResult < 0)
			{
				return true;
			}
			else if (packageNameComparisonResult > 0)
			{
				return false;
			}
			else
			{
				return relationType < other.relationType;
			}
		}
	};
	shared_ptr< const Cache > __cache;
	string __dummy_package_name;
	map< string, set< PackageDependency > > __dependency_map;
	set< string > __processed_dependencies;
	vector< DependencyEntry > __dependency_entries;
	size_t __next_free_id;

	void __add_package_dependencies(const string& packageName);
 public:
	SolutionStorage(const shared_ptr< const Cache >&,
			const vector< DependencyEntry >&, const string& dummyPackageName);
	// should be called only with versions with the same package name
	void addVersionDependencies(const vector< shared_ptr< const BinaryVersion > >&);
	shared_ptr< Solution > cloneSolution(const shared_ptr< Solution >&);
	void setPackageEntry(Solution&, const string& packageName, const PackageEntry&);
	void invalidateReferencedBy(Solution&, const string& packageName);
};

}
}

#endif

