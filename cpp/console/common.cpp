/**************************************************************************
*   Copyright (C) 2012 by Eugene V. Lyubimkin                             *
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
#include "common.hpp"

#include <cupt/system/state.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/sourcepackage.hpp>

bool isPackageInstalled(const Cache& cache, PackageId packageId)
{
	auto&& installedInfo = cache.getSystemState()->getInstalledInfo(packageId);
	return (installedInfo && installedInfo->status != system::State::InstalledRecord::Status::ConfigFiles);
}

template < typename VersionT >
ReverseDependsIndex< VersionT >::ReverseDependsIndex(const Cache& cache)
	: __cache(cache), __architecture(cache.getSystemState()->getArchitecture())
{}

template < typename VersionT >
void ReverseDependsIndex< VersionT >::add(RelationTypeT relationType)
{
	auto insertResult = __data.insert({ relationType, {} });
	if (insertResult.second)
	{
		__add(relationType, &insertResult.first->second);
	}
}

namespace {

template < typename T > struct TraitsPlus;
template<> struct TraitsPlus< BinaryVersion >
{
	static vector< PackageId > getPackageIds(const Cache& cache) { return cache.getBinaryPackageIds(); };
	static const BinaryPackage* getPackage(const Cache& cache, PackageId packageId)
	{ return cache.getBinaryPackage(packageId); }
};
template<> struct TraitsPlus< SourceVersion >
{
	static vector< PackageId > getPackageIds(const Cache& cache) { return cache.getSourcePackageIds(); };
	static const SourcePackage* getPackage(const Cache& cache, PackageId packageId)
	{ return cache.getSourcePackage(packageId); };
};

}

template < typename VersionT >
const RelationLine& ReverseDependsIndex< VersionT >::__getRelationLine(const RelationLine& rl) const
{
	return rl;
}

template < typename VersionT >
RelationLine ReverseDependsIndex< VersionT >::__getRelationLine(const ArchitecturedRelationLine& arl) const
{
	return arl.toRelationLine(__architecture);
}

template < typename VersionT >
void ReverseDependsIndex< VersionT >::__add(RelationTypeT relationType, PerRelationType* storage)
{
	typedef TraitsPlus< VersionT > TP;

	for (PackageId packageId: TP::getPackageIds(__cache))
	{
		set< PackageId > usedKeys;

		auto package = TP::getPackage(__cache, packageId);
		for (const auto& version: *package)
		{
			auto&& relationLine = __getRelationLine(version->relations[relationType]);
			for (const auto& relationExpression: relationLine)
			{
				auto satisfyingVersions = __cache.getSatisfyingVersions(relationExpression);
				for (const auto& satisfyingVersion: satisfyingVersions)
				{
					auto satisfyingPackageId = satisfyingVersion->packageId;
					if (usedKeys.insert(satisfyingPackageId).second)
					{
						(*storage)[satisfyingPackageId].push_back(package);
					}
				}
			}
		}
	}
}

template < typename VersionT >
void ReverseDependsIndex< VersionT >::foreachReverseDependency(
		const BinaryVersion* version, RelationTypeT relationType,
		const std::function< void (const VersionT*, const RelationExpression&) > callback)
{
	auto storageIt = __data.find(relationType);
	if (storageIt == __data.end()) return;
	const auto& storage = storageIt->second;

	auto packageCandidatesIt = storage.find(version->packageId);
	if (packageCandidatesIt != storage.end())
	{
		const auto& packageCandidates = packageCandidatesIt->second;
		for (auto packageCandidate: packageCandidates)
		{
			for (const auto& candidateVersion: *packageCandidate)
			{
				auto&& relationLine = __getRelationLine(candidateVersion->relations[relationType]);
				for (const auto& relationExpression: relationLine)
				{
					auto satisfyingVersions = __cache.getSatisfyingVersions(relationExpression);
					for (const auto& satisfyingVersion: satisfyingVersions)
					{
						if (satisfyingVersion == version)
						{
							callback(candidateVersion, relationExpression);
							goto candidate;
						}
					}
				}
				candidate:
				;
			}
		}
	}
}

template class ReverseDependsIndex< BinaryVersion >;
template class ReverseDependsIndex< SourceVersion >;

