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

#include <cupt/cache.hpp>
#include <cupt/cache/binarypackage.hpp>

#include <internal/nativeresolver/solution.hpp>

namespace cupt {
namespace internal {

PackageEntry::PackageEntry()
	: sticked(false)
{}

class PackageEntryMap
{
 public:
	typedef string key_t; // package name
	typedef pair< key_t, PackageEntry > data_t; // vector requires assignable types
	typedef data_t value_type; // for set_union
	typedef vector< data_t > container_t;
 private:
	container_t __container;
 public:
	size_t forkedCount;

	PackageEntryMap()
		: forkedCount(0)
	{}

	typedef data_t* iterator_t;
	typedef const data_t* const_iterator_t;

	size_t size() const { return __container.size(); }
	void reserve(size_t size) { __container.reserve(size); }
	iterator_t begin() { return &*__container.begin(); }
	iterator_t end() { return &*__container.end(); }
	iterator_t lower_bound(const key_t& key)
	{
		struct Comparator
		{
			bool operator()(const data_t& data, const key_t& key) const
			{ return data.first < key; }
		};
		return std::lower_bound(begin(), end(), key, Comparator());
	}
	iterator_t find(const key_t& key)
	{
		auto result = lower_bound(key);
		if (result != end() && result->first != key)
		{
			result = end();
		}
		return result;
	}
	// this insert() is called only for unexisting elements
	// TODO: &&
	iterator_t insert(iterator_t hint, const data_t& data)
	{
		auto distance = hint - begin();
		__container.insert(static_cast< container_t::iterator >(hint), data);
		return begin() + distance;
	}
	void push_back(const data_t& data)
	{
		__container.push_back(data);
	}
};

SolutionStorage::SolutionStorage(const shared_ptr< const Cache >& cache,
		const vector< DependencyEntry >& dependencyEntries,
		const string& dummyPackageName)
	: __cache(cache), __dummy_package_name(dummyPackageName),
	__dependency_entries(dependencyEntries), __next_free_id(0)
{}

void SolutionStorage::__add_package_dependencies(const string& packageName)
{
	if (!__processed_dependencies.insert(packageName).second)
	{
		return; // already processed entry
	}

	if (packageName == __dummy_package_name)
	{
		return;
	}

	auto package = __cache->getBinaryPackage(packageName);
	if (!package)
	{
		fatal("internal error: unable to find the package '%s'", packageName.c_str());
	}

	addVersionDependencies(package->getVersions());
}

void SolutionStorage::addVersionDependencies(const vector< shared_ptr< const BinaryVersion > >& versions)
{
	const string& packageName = versions[0]->packageName;

	FORIT(dependencyEntryIt, __dependency_entries)
	{
		auto relationType = dependencyEntryIt->type;

		set< string > satisfyingPackageNames;

		FORIT(versionIt, versions)
		{
			const RelationLine& relationLine = (*versionIt)->relations[relationType];
			FORIT(relationExpressionIt, relationLine)
			{
				auto satisfyingVersions = __cache->getSatisfyingVersions(*relationExpressionIt);
				FORIT(satisfyingVersionIt, satisfyingVersions)
				{
					satisfyingPackageNames.insert((*satisfyingVersionIt)->packageName);
				}
			}
		}

		FORIT(satisfyingPackageNameIt, satisfyingPackageNames)
		{
			const string& satisfyingPackageName = *satisfyingPackageNameIt;
			if (satisfyingPackageName == packageName)
			{
				continue;
			}
			__dependency_map[satisfyingPackageName].insert(
					PackageDependency { packageName, relationType });
		}
	}
}

shared_ptr< Solution > SolutionStorage::cloneSolution(const shared_ptr< Solution >& source)
{
	shared_ptr< Solution > cloned(new Solution);
	cloned->score = source->score;
	cloned->level = source->level;
	cloned->id = __next_free_id++;
	cloned->finished = false;

	cloned->__parent = source;

	// other part should be done by calling prepare outside

	return cloned;
}

void SolutionStorage::setPackageEntry(Solution& solution,
		const string& packageName, const PackageEntry& packageEntry)
{
	auto it = solution.__package_entries->lower_bound(packageName);
	if (it == solution.__package_entries->end() || it->first != packageName)
	{
		// there is no modifiable element in this solution, need to create new

		// this package may just appear...
		__add_package_dependencies(packageName);

		it = solution.__package_entries->insert(it, make_pair(packageName, packageEntry));
	}
	else
	{
		it->second = packageEntry;
	}
}

const set< SolutionStorage::PackageDependency >& SolutionStorage::getReferencedSet(const string& packageName)
{
	static const set< PackageDependency > nullSet;

	auto successorsIt = __dependency_map.find(packageName);
	if (successorsIt != __dependency_map.end())
	{
		return successorsIt->second;
	}
	else
	{
		return nullSet;
	}
}



Solution::Solution()
	: id(0), level(0), score(0), finished(false)
{
	__package_entries.reset(new PackageEntryMap);
}

void Solution::prepare()
{
	if (!__parent)
	{
		fatal("internal error: undefined master solution");
	}

	if (!__parent->__master_package_entries)
	{
		// parent solution is a master solution, build a slave on top of it
		__master_package_entries = __parent->__package_entries;
		__package_entries.reset(new PackageEntryMap);
	}
	else
	{
		// this a slave solution
		size_t& forkedCount = __parent->__master_package_entries->forkedCount;
		forkedCount += __parent->__package_entries->size();
		if (forkedCount > __parent->__master_package_entries->size())
		{
			forkedCount = 0;

			// master solution is overdiverted, build new master one
			__package_entries.reset(new PackageEntryMap);
			__package_entries->reserve(__parent->__package_entries->size() +
					__parent->__master_package_entries->size());

			// it's important that parent's __package_entries come first,
			// if two elements are present in both (i.e. an element is overriden)
			// the new version of an element will be written
			struct Comparator
			{
				bool operator()(const PackageEntryMap::data_t& left, const PackageEntryMap::data_t& right)
				{ return left.first < right.first; }
			};
			std::set_union(__parent->__package_entries->begin(), __parent->__package_entries->end(),
					__parent->__master_package_entries->begin(), __parent->__master_package_entries->end(),
					std::back_inserter(*__package_entries), Comparator());
		}
		else
		{
			// build new slave solution from current
			__master_package_entries = __parent->__master_package_entries;
			*__package_entries = *(__parent->__package_entries);
		}
	}

	__parent.reset();
}

vector< string > Solution::getPackageNames() const
{
	vector< string > result;

	if (__master_package_entries)
	{
		FORIT(it, *__master_package_entries)
		{
			result.push_back(it->first);
		}
	}
	auto middleSize = result.size();
	FORIT(it, *__package_entries)
	{
		result.push_back(it->first);
	}

	std::inplace_merge(result.begin(), result.begin() + middleSize, result.end());
	result.erase(std::unique(result.begin(), result.end()), result.end());

	return result;
}

vector< const string* > Solution::getUncheckedPackageNames(
		RelationType dependencyType) const
{
	vector< const string* > result;
	auto isEligible = [&dependencyType](decltype(__package_entries->begin()) it)
	{
		return (!it->second.checked[dependencyType] && it->second.version);
	};
	auto processEntry = [&result, &isEligible](decltype(__package_entries->begin()) it)
	{
		if (isEligible(it))
		{
			result.push_back(&it->first);
		}
	};

	auto masterIt = __master_package_entries ?
			__master_package_entries->begin() : __package_entries->end();
	auto masterEnd = __master_package_entries ?
			__master_package_entries->end() : __package_entries->end();
	auto ownIt = __package_entries->begin();
	auto ownEnd = __package_entries->end();

	while (masterIt != masterEnd && ownIt != ownEnd)
	{
		// compare is expensive operation and isEligible is cheap, usually most
		// of masterIt won't be included anyway
		if (!isEligible(masterIt))
		{
			++masterIt;
			continue;
		}

		auto compareResult = masterIt->first.compare(ownIt->first);
		if (compareResult < 0)
		{
			processEntry(masterIt);
			++masterIt;
		}
		else if (compareResult > 0)
		{
			processEntry(ownIt);
			++ownIt;
		}
		else // equal keys
		{
			// own entry overrides master entry
			processEntry(ownIt);
			++masterIt;
			++ownIt;
		}
	}
	while (masterIt != masterEnd)
	{
		processEntry(masterIt);
		++masterIt;
	}
	while (ownIt != ownEnd)
	{
		processEntry(ownIt);
		++ownIt;
	}

	return result;
}

bool Solution::getPackageEntry(const string& packageName, PackageEntry* result) const
{
	auto it = __package_entries->find(packageName);
	if (it != __package_entries->end())
	{
		if (result)
		{
			*result = it->second;
		}
		return true;
	}
	if (__master_package_entries)
	{
		it = __master_package_entries->find(packageName);
		if (it != __master_package_entries->end())
		{
			if (result)
			{
				*result = it->second;
			}
			return true;
		}
	}

	// not found
	return false;
}

}
}

