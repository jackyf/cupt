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

bool isPackageInstalled(const Cache& cache, PackageId packageId)
{
	auto&& installedInfo = cache.getSystemState()->getInstalledInfo(packageId);
	return (installedInfo && installedInfo->status != system::State::InstalledRecord::Status::ConfigFiles);
}

typedef BinaryVersion::RelationTypes BRT;

ReverseDependsIndex::ReverseDependsIndex(const Cache& cache)
	: __cache(cache)
{}

void ReverseDependsIndex::add(BRT::Type relationType)
{
	auto insertResult = __data.insert({ relationType, {} });
	if (insertResult.second)
	{
		__add(relationType, &insertResult.first->second);
	}
}

void ReverseDependsIndex::__add(BRT::Type relationType, PerRelationType* storage)
{
	for (auto packageId: __cache.getBinaryPackageIds())
	{
		set< PackageId > usedKeys;

		auto package = __cache.getBinaryPackage(packageId);
		for (const auto& version: *package)
		{
			const RelationLine& relationLine = version->relations[relationType];
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

void ReverseDependsIndex::foreachReverseDependency(
		const BinaryVersion* version, BRT::Type relationType,
		const std::function< void (const BinaryVersion*, const RelationExpression&) > callback)
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
				for (const auto& relationExpression: candidateVersion->relations[relationType])
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

