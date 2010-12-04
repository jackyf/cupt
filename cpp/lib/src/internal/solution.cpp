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

#include <internal/solution.hpp>

namespace cupt {
namespace internal {

PackageEntry::PackageEntry()
	: sticked(false)
{}

SolutionStorage::SolutionStorage(const shared_ptr< const Cache >& cache,
		const vector< DependencyEntry >& dependencyEntries,
		const string& dummyPackageName)
	: __cache(cache), __dummy_package_name(dummyPackageName),
	__dependency_entries(dependencyEntries), __next_free_id(0)
{}

void SolutionStorage::__add_package_dependencies(const string& packageName)
{
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
		set< string > satisfyingPackageNames;

		FORIT(versionIt, versions)
		{
			const RelationLine& relationLine = (*versionIt)->relations[dependencyEntryIt->type];
			FORIT(relationExpressionIt, relationLine)
			{
				auto satisfyingVersions = __cache->getSatisfyingVersions(*relationExpressionIt);
				FORIT(satisfyingVersionIt, satisfyingVersions)
				{
					satisfyingPackageNames.insert((*satisfyingVersionIt)->packageName);
				}
			}
		}

		bool isAnti = dependencyEntryIt->isAnti;
		FORIT(satisfyingPackageNameIt, satisfyingPackageNames)
		{
			const string& satisfyingPackageName = *satisfyingPackageNameIt;
			if (satisfyingPackageName == packageName)
			{
				continue;
			}
			__dependency_graph.addEdge(satisfyingPackageName, packageName);
			if (isAnti)
			{
				__dependency_graph.addEdge(packageName, satisfyingPackageName);
			}
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
	cloned->pendingAction = source->pendingAction;

	cloned->__parent = source;

	// other part should be done by calling prepare outside

	return cloned;
}

PackageEntry* SolutionStorage::setPackageEntry(const shared_ptr< Solution >& solution,
		const string& packageName)
{
	auto it = solution->__package_entries->find(packageName);

	if (it == solution->__package_entries->end())
	{
		// there is no modifiable element in this solution, need to create new

		pair< const string, PackageEntry > newElement(packageName, PackageEntry()); // package entry is empty by default

		if (!solution->__master_package_entries)
		{
			// this package may just appear...
			__add_package_dependencies(packageName);
		}
		else
		{
			// let's see if master package entries contain one
			auto masterIt = solution->__master_package_entries->find(packageName);
			if (masterIt != solution->__master_package_entries->end())
			{
				// yes, it does, so new package entry must be a copy of it
				newElement.second = masterIt->second;
			}
			else
			{
				// this package may just appear...
				__add_package_dependencies(packageName);
			}
		}
		it = solution->__package_entries->insert(newElement).first;
	}

	PackageEntry* result = &(it->second);

	__invalidate(solution, packageName, result);

	return result;
}

void SolutionStorage::__invalidate(const shared_ptr< Solution >& solution,
		const string& packageName, PackageEntry* packageEntry)
{
	packageEntry->checked.reset();
	const list< const string* >& successorPackageNamePtrs = __dependency_graph.getSuccessors(packageName);
	FORIT(successorPackageNamePtrIt, successorPackageNamePtrs)
	{
		const string& successorPackageName = **successorPackageNamePtrIt;

		auto it = solution->__package_entries->find(successorPackageName);
		if (it == solution->__package_entries->end())
		{
			if (!solution->__master_package_entries)
			{
				// no such package entry in this solution
				continue;
			}

			auto masterIt = solution->__master_package_entries->find(successorPackageName);
			if (masterIt == solution->__master_package_entries->end())
			{
				// no such package entry in this solution
				continue;
			}

			// this is package entry from _master_packages, and we change it, so we
			// need to clone it
			pair< const string, PackageEntry > newElement(successorPackageName, masterIt->second);
			it = solution->__package_entries->insert(newElement).first;
		}
		it->second.checked.reset();
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
		static const float overdivertedFactor = 0.7;
		if (__parent->__package_entries->size() >=
			pow(__parent->__master_package_entries->size(), overdivertedFactor))
		{
			// master solution is overdiverted, build new master one
			*__package_entries = *(__parent->__master_package_entries);
			FORIT(packageEntryIt, *__parent->__package_entries)
			{
				const string& key = packageEntryIt->first;
				const PackageEntry& value = packageEntryIt->second;
				(*__package_entries)[key] = value;
			}
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

vector< const string* > Solution::getMostlyUncheckedPackageNames(
		BinaryVersion::RelationTypes::Type dependencyType) const
{
	vector< const string* > result;

	if (__master_package_entries)
	{
		FORIT(it, *__master_package_entries)
		{
			if (!it->second.checked.test(dependencyType))
			{
				result.push_back(&(it->first));
			}
		}
	}
	auto middleSize = result.size();
	FORIT(it, *__package_entries)
	{
		if (!it->second.checked.test(dependencyType))
		{
			result.push_back(&(it->first));
		}
	}

	std::inplace_merge(result.begin(), result.begin() + middleSize, result.end(), PointerLess< string >());
	result.erase(std::unique(result.begin(), result.end(), PointerEqual< string >()), result.end());

	return result;
}

const PackageEntry* Solution::getPackageEntry(const string& packageName) const
{
	auto it = __package_entries->find(packageName);
	if (it != __package_entries->end())
	{
		return &(it->second);
	}
	if (__master_package_entries)
	{
		it = __master_package_entries->find(packageName);
		if (it != __master_package_entries->end())
		{
			return &(it->second);
		}
	}

	// not found
	return NULL;
}

PackageEntry* Solution::setPackageEntryIfExists(const string& packageName)
{
	auto it = __package_entries->find(packageName);
	if (it != __package_entries->end())
	{
		return &(it->second);
	}
	if (__master_package_entries)
	{
		it = __master_package_entries->find(packageName);
		if (it != __master_package_entries->end())
		{
			const PackageEntry& oldPackageEntry = it->second;
			return &(__package_entries->insert(make_pair(packageName, oldPackageEntry)).first->second);
		}
	}

	// not found
	return NULL;
}

}
}

