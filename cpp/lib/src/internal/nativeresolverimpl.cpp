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
// TODO/2.1: define options for numbers used in resolver

#include <cmath>
#include <queue>
#include <algorithm>

#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/sourcepackage.hpp>
#include <cupt/cache/sourceversion.hpp>
#include <cupt/regex.hpp>

#include <internal/nativeresolverimpl.hpp>
#include <internal/graph.hpp>

namespace cupt {
namespace internal {

using std::queue;

NativeResolverImpl::NativeResolverImpl(const shared_ptr< const Config >& config, const shared_ptr< const Cache >& cache)
	: __config(config), __cache(cache),
	__solution_storage(__cache, __get_dependency_groups(), __dummy_package_name),
	__old_solution(new Solution)
{
	__import_installed_versions();
}

void NativeResolverImpl::__import_installed_versions()
{
	auto versions = __cache->getInstalledVersions();
	// __initial_solution will be modified, leave __old_solution as original system state
	FORIT(versionIt, versions)
	{
		// just moving versions, don't try to install or remove some dependencies
		const shared_ptr< const BinaryVersion > version = *versionIt;
		const string& packageName = version->packageName;

		auto packageEntry = __solution_storage.setPackageEntry(__old_solution, packageName);
		packageEntry->version = version;

		__installed_package_names.insert(packageName);
	}
	__initial_solution = __solution_storage.cloneSolution(__old_solution);
	__initial_solution->prepare();
}

vector< DependencyEntry > NativeResolverImpl::__get_dependency_groups() const
{
	vector< DependencyEntry > result = {
		{ BinaryVersion::RelationTypes::PreDepends, false },
		{ BinaryVersion::RelationTypes::Depends, false },
		{ BinaryVersion::RelationTypes::Conflicts, true },
		{ BinaryVersion::RelationTypes::Breaks, true },
	};

	if (__config->getBool("cupt::resolver::keep-recommends"))
	{
		result.push_back(DependencyEntry { BinaryVersion::RelationTypes::Recommends, false });
	}
	if (__config->getBool("cupt::resolver::keep-suggests"))
	{
		result.push_back(DependencyEntry { BinaryVersion::RelationTypes::Suggests, false });
	}

	return result;
}

void __mydebug_wrapper(const shared_ptr< Solution >& solution, const string& message)
{
	string levelString(solution->level, ' ');
	debug("%s(%u:%.1f) %s", levelString.c_str(), solution->id, solution->score, message.c_str());
}

vector< string > __get_related_binary_package_names(const shared_ptr< const Cache >& cache,
		const shared_ptr< Solution >& solution, const shared_ptr< const BinaryVersion >& version)
{
	vector< string > result;

	const string& packageName = version->packageName;
	const string& sourcePackageName = version->sourcePackageName;

	vector< string > possiblyRelatedPackageNames;

	auto sourcePackage = cache->getSourcePackage(sourcePackageName);
	if (sourcePackage)
	{
		auto sourceVersion = static_pointer_cast< const SourceVersion >(
				sourcePackage->getSpecificVersion(version->sourceVersionString));
		if (sourceVersion)
		{
			auto binaryPackageNames = sourceVersion->binaryPackageNames;
			FORIT(binaryPackageNameIt, binaryPackageNames)
			{
				if (solution->getPackageEntry(*binaryPackageNameIt))
				{
					possiblyRelatedPackageNames.push_back(*binaryPackageNameIt);
				}
			}
		}
	}
	if (possiblyRelatedPackageNames.empty())
	{
		possiblyRelatedPackageNames = solution->getPackageNames();
	}

	FORIT(possiblyRelatedPackageNameIt, possiblyRelatedPackageNames)
	{
		const string& possiblyRelatedPackageName = *possiblyRelatedPackageNameIt;
		if (possiblyRelatedPackageName == packageName)
		{
			continue;
		}
		auto otherVersion = solution->getPackageEntry(possiblyRelatedPackageName)->version;
		if (!otherVersion)
		{
			continue;
		}
		if (otherVersion->sourcePackageName != sourcePackageName)
		{
			continue;
		}
		result.push_back(possiblyRelatedPackageName);
	}

	return result;
}

shared_ptr< const BinaryVersion > __get_version_by_source_version_string(
		const shared_ptr< const Cache >& cache, const string& packageName,
		const string& sourceVersionString)
{
	auto versions = cache->getBinaryPackage(packageName)->getVersions();
	FORIT(versionIt, versions)
	{
		if ((*versionIt)->sourceVersionString == sourceVersionString)
		{
			return *versionIt;
		}
	}

	return shared_ptr< const BinaryVersion >();
}

vector< string > NativeResolverImpl::__get_unsynchronizeable_related_package_names(const shared_ptr< Solution >& solution,
		const shared_ptr< const BinaryVersion >& version)
{
	const string& sourcePackageName = version->sourcePackageName;
	auto regexExceptionList = __config->getList("cupt::resolver::synchronize-source-versions::exceptions");
	smatch m;
	FORIT(exceptionalRegexStringIt, regexExceptionList)
	{
		const string& regexString = *exceptionalRegexStringIt;
		try
		{
			const sregex regex = sregex::compile(regexString);
			if (regex_match(sourcePackageName, m, regex))
			{
				return vector< string >();
			}
		}
		catch (regex_error&)
		{
			fatal("regular expression '%s' is not valid", regexString.c_str());
		}
	}
	const string& sourceVersionString = version->sourceVersionString;
	vector< string > relatedPackageNames = __get_related_binary_package_names(__cache, solution, version);

	vector< string > result;

	FORIT(relatedPackageNameIt, relatedPackageNames)
	{
		const string& relatedPackageName = *relatedPackageNameIt;

		auto relatedPackageEntry = solution->getPackageEntry(relatedPackageName);
		auto relatedVersion = relatedPackageEntry->version;
		if (relatedVersion->sourceVersionString == sourceVersionString)
		{
			continue; // no update needed
		}

		if (relatedPackageEntry->sticked ||
			!__get_version_by_source_version_string(__cache, relatedPackageName, sourceVersionString))
		{
			// cannot update the package
			result.push_back(relatedPackageName);
		}
	}

	return result;
}

bool NativeResolverImpl::__can_related_packages_be_synchronized(
		const shared_ptr< Solution >& solution, const shared_ptr< const BinaryVersion >& version)
{
	return __get_unsynchronizeable_related_package_names(solution, version).empty();
}

void NativeResolverImpl::__synchronize_related_packages(const shared_ptr< Solution >& solution,
		const shared_ptr< const BinaryVersion >& version, bool stick)
{
	auto relatedPackageNames = __get_related_binary_package_names(__cache, solution, version);
	const string& sourceVersionString = version->sourceVersionString;
	const string& packageName = version->packageName;

	auto debugging = __config->getBool("debug::resolver");
	auto trackReasons = __config->getBool("cupt::resolver::track-reasons");
	FORIT(relatedPackageNameIt, relatedPackageNames)
	{
		const string& relatedPackageName = *relatedPackageNameIt;
		auto packageEntry = solution->getPackageEntry(relatedPackageName);
		if (packageEntry->sticked)
		{
			continue;
		}
		if (packageEntry->version->sourceVersionString == sourceVersionString)
		{
			continue;
		}
		auto candidateVersion = __get_version_by_source_version_string(__cache,
				relatedPackageName, sourceVersionString);
		if (!candidateVersion)
		{
			continue;
		}

		auto modifiedPackageEntry = __solution_storage.setPackageEntry(solution, relatedPackageName);

		modifiedPackageEntry->version = candidateVersion;
		modifiedPackageEntry->sticked = stick;
		if (debugging)
		{
			__mydebug_wrapper(solution,
					sf("synchronizing package '%s' with package '%s'",
							relatedPackageName.c_str(), packageName.c_str()));
		}
		if (trackReasons)
		{
			modifiedPackageEntry->reasons.push_back(
					shared_ptr< const Reason >(new SynchronizationReason(packageName)));
		}
	}
}

// installs new version, but does not sticks it
NativeResolverImpl::InstallVersionResult::Type
NativeResolverImpl::__install_version_no_stick(const shared_ptr< const BinaryVersion >& version,
		const shared_ptr< const Reason >& reason, PackageEntry*& packageEntry)
{
	const string& packageName = version->packageName;

	packageEntry = __solution_storage.setPackageEntry(__initial_solution, packageName);

	{ // maybe, nothing changed?
		const shared_ptr< const BinaryVersion >& currentVersion = packageEntry->version;
		if (currentVersion && currentVersion->versionString == version->versionString)
		{
			return InstallVersionResult::Ok;
		}
	}

	if (packageEntry->sticked)
	{
		// package is restricted to be updated
		return InstallVersionResult::Restricted;
	}

	auto synchronize = __config->getString("cupt::resolver::synchronize-source-versions");
	if (synchronize == "hard")
	{
		// need to check is the whole operation doable
		if (!__can_related_packages_be_synchronized(__initial_solution, version))
		{
			// we cannot do it
			return InstallVersionResult::Unsynchronizeable;
		}
	}

	// update the requested package
	packageEntry->version = version;
	if (__config->getBool("cupt::resolver::track-reasons"))
	{
		packageEntry->reasons.push_back(reason);
	}
	if (__config->getBool("debug::resolver"))
	{
		debug("install package '%s', version '%s'",
				packageName.c_str(), version->versionString.c_str());
	}

	if (synchronize != "none")
	{
		__synchronize_related_packages(__initial_solution, version, false);
	}

	return InstallVersionResult::Ok;
}

void NativeResolverImpl::installVersion(const shared_ptr< const BinaryVersion >& version)
{
	const string& packageName = version->packageName;

	PackageEntry* packageEntry;
	shared_ptr< const Reason > reason(new UserReason());
	auto installResult = __install_version_no_stick(version, reason, packageEntry);
	if (installResult == InstallVersionResult::Restricted)
	{
		fatal("unable to re-schedule package '%s'", packageName.c_str());
	}
	else if (installResult == InstallVersionResult::Unsynchronizeable)
	{
		fatal("unable to synchronize related binary packages for %s %s'",
					packageName.c_str(), version->versionString.c_str());
	}
	else
	{
		if (installResult != InstallVersionResult::Ok)
		{
			fatal("internal error: unknown install result");
		}
	}

	packageEntry->sticked = true;
	__manually_modified_package_names.insert(packageName);
}

void NativeResolverImpl::satisfyRelationExpression(const RelationExpression& relationExpression)
{
	__satisfy_relation_expressions.push_back(relationExpression);
	if (__config->getBool("debug::resolver"))
	{
		debug("strictly satisfying relation '%s'", relationExpression.toString().c_str());
	}
}

void NativeResolverImpl::unsatisfyRelationExpression(const RelationExpression& relationExpression)
{
	__unsatisfy_relation_expressions.push_back(relationExpression);
	if (__config->getBool("debug::resolver"))
	{
		debug("strictly unsatisfying relation '%s'", relationExpression.toString().c_str());
	}
}

void NativeResolverImpl::removePackage(const string& packageName)
{
	auto packageEntry = __solution_storage.setPackageEntry(__initial_solution, packageName);

	if (packageEntry->version && packageEntry->sticked)
	{
		fatal("unable to re-schedule package '%s'", packageName.c_str());
	}

	__manually_modified_package_names.insert(packageName);

	packageEntry->version.reset();
	packageEntry->sticked = true;

	if (__config->getBool("cupt::resolver::track-reasons"))
	{
		packageEntry->reasons.push_back(shared_ptr< const Reason >(new UserReason));
	}
	if (__config->getBool("debug::resolver"))
	{
		debug("removing package '%s'", packageName.c_str());
	}
}

void NativeResolverImpl::upgrade()
{
	const shared_ptr< const Reason > reason(new UserReason);

	auto packageNames = __initial_solution->getPackageNames();
	FORIT(packageNameIt, packageNames)
	{
		const string& packageName = *packageNameIt;
		auto package = __cache->getBinaryPackage(packageName);
		auto originalVersion = __initial_solution->getPackageEntry(packageName)->version;
		if (!originalVersion)
		{
			continue;
		}
		// if there is original version, then at least one policy version should exist
		auto supposedVersion = static_pointer_cast< const BinaryVersion >
				(__cache->getPolicyVersion(package));
		if (!supposedVersion)
		{
			fatal("internal error: supposed version doesn't exist");
		}

		// no need to install the same version
		if (originalVersion->versionString == supposedVersion->versionString)
		{
			continue;
		}

		PackageEntry* unused;
		__install_version_no_stick(supposedVersion, reason, unused);
	}
}

NativeResolverImpl::SolutionListIterator __fair_chooser(list< shared_ptr< Solution > >& solutions)
{
	// choose the solution with maximum score
	auto result = solutions.begin();

	auto it = result;
	++it;

	for (; it != solutions.end(); ++it)
	{
		if (! *it)
		{
			fatal("internal error: an empty solution");
		}
		if ((*it)->score > (*result)->score)
		{
			result = it;
		}
	}

	return result;
}

NativeResolverImpl::SolutionListIterator __full_chooser(list< shared_ptr< Solution > >& solutions)
{
	// defer the decision until all solutions are built

	FORIT(solutionIt, solutions)
	{
		if (! (*solutionIt)->finished)
		{
			return solutionIt;
		}
	}

	// heh, the whole solution tree has been already built?.. ok, let's choose
	// the best solution
	return __fair_chooser(solutions);
}

// every package version has a weight
float NativeResolverImpl::__get_version_weight(const shared_ptr< const BinaryVersion >& version) const
{
	if (!version)
	{
		return 0.0;
	}

	float result = __cache->getPin(version);

	if (version->essential)
	{
		result *= 5.0;
	}

	// omitting priority 'standard' here
	switch (version->priority)
	{
		case Version::Priorities::Optional:
			result *= 0.9;
			break;
		case Version::Priorities::Extra:
			result *= 0.7;
			break;
		case Version::Priorities::Important:
			result *= 1.4;
			break;
		case Version::Priorities::Required:
			result *= 2.0;
			break;
		default:
			;
			// do nothing
	}

	if (result > 0)
	{
		// apply the rules only to positive results so negative ones save their
		// negative effect
		const string& packageName = version->packageName;

		if (__cache->isAutomaticallyInstalled(packageName))
		{
			result /= 12.0;
		}
		else if (!__old_solution->getPackageEntry(packageName))
		{
			// it's new package
			result /= 100.0;
		}
	}

	return result;
}

float NativeResolverImpl::__get_action_profit(const shared_ptr< const BinaryVersion >& originalVersion,
		const shared_ptr< const BinaryVersion >& supposedVersion) const
{
	auto supposedVersionWeight = __get_version_weight(supposedVersion);
	auto originalVersionWeight = __get_version_weight(originalVersion);

	if (!originalVersion)
	{
		// installing the version itself gains nothing
		supposedVersionWeight -= 15;
	}

	auto result = supposedVersionWeight - originalVersionWeight;

	if (!supposedVersion)
	{
		// remove a package
		result -= 50;
		if (result < 0)
		{
			result *= 4;
		}
	}

	return result;
}

const shared_ptr< const BinaryVersion >* __is_version_array_intersects_with_packages(
		const vector< shared_ptr< const BinaryVersion > >& versions,
		const shared_ptr< Solution >& solution, const string* ignorePackageNamePtr = NULL)
{
	FORIT(versionIt, versions)
	{
		const shared_ptr< const BinaryVersion >& version = *versionIt;
		const string& packageName = version->packageName;

		if (ignorePackageNamePtr && packageName == *ignorePackageNamePtr)
		{
			continue;
		}

		auto packageEntry = solution->getPackageEntry(packageName);
		if (!packageEntry)
		{
			continue;
		}

		const shared_ptr< const BinaryVersion >& solutionVersion = packageEntry->version;
		if (!solutionVersion)
		{
			continue;
		}

		if (version->versionString == solutionVersion->versionString)
		{
			return &version;
		}
	}

	return NULL;
}

bool NativeResolverImpl::__can_package_be_removed(const string& packageName) const
{
	return !__config->getBool("cupt::resolver::no-remove") ||
			!__installed_package_names.count(packageName) ||
			__cache->isAutomaticallyInstalled(packageName);
}

void NativeResolverImpl::__clean_automatically_installed(const shared_ptr< Solution >& solution)
{
	vector< sregex > neverAutoRemoveRegexes;
	{
		auto neverAutoRemoveRegexStrings = __config->getList("apt::neverautoremove");

		FORIT(regexStringIt, neverAutoRemoveRegexStrings)
		{
			try
			{
				neverAutoRemoveRegexes.push_back(sregex::compile(*regexStringIt));
			}
			catch (regex_error&)
			{
				fatal("invalid regular expression '%s'", regexStringIt->c_str());
			}
		}
	}

	// firstly, prepare all package names that can be potentially removed
	auto canAutoremove = __config->getBool("cupt::resolver::auto-remove");
	set< string > candidatesForRemoval;

	smatch m;

	auto packageNames = solution->getPackageNames();
	FORIT(packageNameIt, packageNames)
	{
		const string& packageName = *packageNameIt;
		if (packageName == __dummy_package_name)
		{
			continue;
		}

		auto packageEntry = solution->getPackageEntry(packageName);
		const shared_ptr< const BinaryVersion >& version = packageEntry->version;
		if (!version)
		{
			continue;
		}

		if (__manually_modified_package_names.count(packageName))
		{
			continue;
		}

		if (version->essential)
		{
			continue;
		}

		auto canAutoremoveThisPackage = canAutoremove && __cache->isAutomaticallyInstalled(packageName);
		bool packageWasInstalled = (__installed_package_names.count(packageName));
		if (packageWasInstalled && !canAutoremoveThisPackage)
		{
			continue;
		}

		bool eligible = true;
		FORIT(regexIt, neverAutoRemoveRegexes)
		{
			if (regex_match(packageName, m, *regexIt))
			{
				eligible = false;
				break;
			}
		}

		if (eligible)
		{
			// ok, candidate for removing
			candidatesForRemoval.insert(packageName);
		}
	}

	bool keepRecommends = __config->getBool("cupt::resolver::keep-recommends");
	bool keepSuggests = __config->getBool("cupt::resolver::keep-suggests");

	Graph< string > dependencyGraph;
	static const string mainVertexPackageName = "main@vertex";
	{ // building dependency graph
		FORIT(packageNameIt, packageNames)
		{
			const string& packageName = *packageNameIt;

			const shared_ptr< const BinaryVersion >& version = solution->getPackageEntry(packageName)->version;
			if (!version)
			{
				continue;
			}

			RelationLine valuableRelationLine;
			valuableRelationLine.insert(valuableRelationLine.end(),
					version->relations[BinaryVersion::RelationTypes::PreDepends].begin(),
					version->relations[BinaryVersion::RelationTypes::PreDepends].end());
			valuableRelationLine.insert(valuableRelationLine.end(),
					version->relations[BinaryVersion::RelationTypes::Depends].begin(),
					version->relations[BinaryVersion::RelationTypes::Depends].end());
			if (keepRecommends)
			{
				valuableRelationLine.insert(valuableRelationLine.end(),
						version->relations[BinaryVersion::RelationTypes::Recommends].begin(),
						version->relations[BinaryVersion::RelationTypes::Recommends].end());
			}
			if (keepSuggests)
			{
				valuableRelationLine.insert(valuableRelationLine.end(),
						version->relations[BinaryVersion::RelationTypes::Suggests].begin(),
						version->relations[BinaryVersion::RelationTypes::Suggests].end());
			}

			FORIT(relationExpressionIt, valuableRelationLine)
			{
				auto satisfyingVersions = __cache->getSatisfyingVersions(*relationExpressionIt);
				FORIT(satisfyingVersionIt, satisfyingVersions)
				{
					const shared_ptr< const BinaryVersion >& satisfyingVersion = *satisfyingVersionIt;
					const string& candidatePackageName = satisfyingVersion->packageName;

					if (!candidatesForRemoval.count(candidatePackageName))
					{
						continue;
					}

					const shared_ptr< const BinaryVersion >& candidateVersion =
							solution->getPackageEntry(candidatePackageName)->version;

					if (satisfyingVersion->versionString != candidateVersion->versionString)
					{
						continue;
					}

					// well, this is what we need
					if (candidatesForRemoval.count(packageName))
					{
						// this is a relation between two weak packages
						dependencyGraph.addEdge(packageName, candidatePackageName);
					}
					else
					{
						// this is a relation between installed system and a weak package
						dependencyGraph.addEdge(mainVertexPackageName, candidatePackageName);
					}
				}
			}
		}
	}

	{ // looping through the candidates
		bool trackReasons = __config->getBool("cupt::resolver::track-reasons");
		bool debugging = __config->getBool("debug::resolver");

		auto reachableVertexes = dependencyGraph.getReachable(mainVertexPackageName);

		shared_ptr< const Reason > reason(new AutoRemovalReason);

		FORIT(packageNameIt, candidatesForRemoval)
		{
			const string& packageName = *packageNameIt;
			if (!reachableVertexes.count(packageName))
			{
				// surely exists because of candidatesForRemoval :)
				auto packageEntry = solution->setPackageEntryIfExists(packageName);

				packageEntry->version.reset();;
				if (trackReasons)
				{
					// leave only one reason :)
					vector< shared_ptr< const Reason > >& reasons = packageEntry->reasons;
					reasons.clear();
					reasons.push_back(reason);
				}
				if (debugging)
				{
					debug("auto-removed package '%s'", packageName.c_str());
				}
			}
		}
	}
}

NativeResolverImpl::SolutionChooser NativeResolverImpl::__select_solution_chooser() const
{
	SolutionChooser result;

	auto resolverType = __config->getString("cupt::resolver::type");
	if (resolverType == "fair")
	{
		result = __fair_chooser;
	}
	else if (resolverType == "full")
	{
		result = __full_chooser;
	}
	else
	{
		fatal("wrong resolver type '%s'", resolverType.c_str());
	}

	return result;
}

void NativeResolverImpl::__require_strict_relation_expressions()
{
	// "installing" virtual package, which will be used for strict '(un)satisfy' requests
	shared_ptr< BinaryVersion > version(new BinaryVersion);

	version->packageName = __dummy_package_name;
	version->sourcePackageName = __dummy_package_name;
	version->versionString = "";
	version->relations[BinaryVersion::RelationTypes::Depends] = __satisfy_relation_expressions;
	version->relations[BinaryVersion::RelationTypes::Breaks] = __unsatisfy_relation_expressions;

	auto packageEntry = __solution_storage.setPackageEntry(__initial_solution, __dummy_package_name);
	packageEntry->version = version;
	packageEntry->sticked = true;
	__solution_storage.addVersionDependencies(version);
}

/* __pre_apply_action only prints debug info and changes level/score of the
   solution, not modifying packages in it, economing RAM and CPU,
   __post_apply_action will perform actual changes when the solution is picked up
   by resolver */

void NativeResolverImpl::__pre_apply_action(const shared_ptr< Solution >& originalSolution,
		const shared_ptr< Solution >& solution, const Action& actionToApply)
{
	if (originalSolution->finished)
	{
		fatal("internal error: an attempt to make changes to already finished solution");
	}

	const string& packageName = actionToApply.packageName;
	const shared_ptr< const BinaryVersion >& supposedVersion = actionToApply.version;

	auto originalPackageEntry = originalSolution->getPackageEntry(packageName);
	shared_ptr< const BinaryVersion > originalVersion;
	if (originalPackageEntry)
	{
		originalVersion = originalPackageEntry->version;
	}

	auto profit = actionToApply.profit;
	if (isnan(profit))
	{
		profit = __get_action_profit(originalVersion, supposedVersion);
	}

	// temporarily lower the score of the current solution to implement back-tracking
	// the bigger quality bar, the bigger chance for other solutions
	float qualityCorrection = - float(__config->getInteger("cupt::resolver::quality-bar")) /
			pow((originalSolution->level + 1), 0.1);

	if (__config->getBool("debug::resolver"))
	{
		static const string notInstalled = "<not installed>";
		auto oldVersionString = (originalVersion ? originalVersion->versionString : notInstalled);
		auto newVersionString = (supposedVersion ? supposedVersion->versionString : notInstalled);

		auto profitString = sf("%+.1f", profit);
		auto qualityCorrectionString = sf("%+.1f", qualityCorrection);

		auto message = sf("-> (%u,Δ:%s,qΔ:%s) trying: package '%s': '%s' -> '%s'",
				solution->id, profitString.c_str(), qualityCorrectionString.c_str(),
				packageName.c_str(), oldVersionString.c_str(), newVersionString.c_str());
		__mydebug_wrapper(originalSolution, message);
	}

	solution->level += 1;
	solution->score += profit;
	solution->score += qualityCorrection;

	solution->pendingAction = shared_ptr< const Action >(new Action(actionToApply));
}

void NativeResolverImpl::__calculate_profits(const shared_ptr< Solution >& solution,
		vector< Action >& actions) const
{
	size_t positionPenalty = 0;
	FORIT(actionIt, actions)
	{
		Action& action = *actionIt;

		auto packageEntry = solution->getPackageEntry(action.packageName);
		shared_ptr< const BinaryVersion > originalVersion;
		if (packageEntry)
		{
			originalVersion = packageEntry->version;
		}

		if (isnan(action.profit))
		{
			action.profit = __get_action_profit(originalVersion, action.version);
		}
		action.profit -= positionPenalty;

		++positionPenalty;
	}
}

void NativeResolverImpl::__pre_apply_actions_to_solution_tree(list< shared_ptr< Solution > >& solutions,
		const shared_ptr< Solution >& currentSolution, const vector< Action >& actions)
{
	// sort them by "rank", from more good to more bad
	auto sortedActions = actions;
	std::stable_sort(sortedActions.begin(), sortedActions.end(),
			[](const Action& left, const Action& right) -> bool
			{
				return right.profit < left.profit;
			});

	// fork the solution entry and apply all the solutions by one
	FORIT(actionIt, sortedActions)
	{
		// clone the current stack to form a new one
		auto clonedSolution = __solution_storage.cloneSolution(currentSolution);

		solutions.push_back(clonedSolution);

		// apply the solution
		__pre_apply_action(currentSolution, clonedSolution, *actionIt);
	}
}

void NativeResolverImpl::__erase_worst_solutions(list< shared_ptr< Solution > >& solutions)
{
	// don't allow solution tree to grow unstoppably
	size_t maxSolutionCount = __config->getInteger("cupt::resolver::max-solution-count");
	auto debugging = __config->getBool("debug::resolver");
	while (solutions.size() > maxSolutionCount)
	{
		// find the worst solution and drop it
		auto worstSolutionIt = solutions.begin();
		auto solutionIt = solutions.begin();
		++solutionIt;
		for (; solutionIt != solutions.end(); ++solutionIt)
		{
			if ((*solutionIt)->score < (*worstSolutionIt)->score)
			{
				worstSolutionIt = solutionIt;
			}
		}

		if (debugging)
		{
			__mydebug_wrapper(*worstSolutionIt, "dropped");
		}
		solutions.erase(worstSolutionIt);
	}
}

void NativeResolverImpl::__post_apply_action(const shared_ptr< Solution >& solution)
{
	if (!solution->pendingAction)
	{
		fatal("internal error: __post_apply_action: no action to apply");
	}
	const Action& action = *(static_pointer_cast< const Action >(solution->pendingAction));

	const string& packageToModifyName = action.packageName;
	const shared_ptr< const BinaryVersion >& supposedVersion = action.version;

	{ // stick all additionally requested package names
		FORIT(packageNameIt, action.packageToStickNames)
		{
			auto packageEntry = __solution_storage.setPackageEntry(solution, *packageNameIt);
			packageEntry->sticked = true;
		}
	};

	auto packageEntry = __solution_storage.setPackageEntry(solution, packageToModifyName);
	packageEntry->version = supposedVersion;
	packageEntry->sticked = true;

	if(action.fakelySatisfies)
	{
		packageEntry->fakelySatisfied.push_back(*(action.fakelySatisfies));
	}
	else if (action.reason)
	{
		packageEntry->reasons.push_back(action.reason);
	}

	if (__config->getString("cupt::resolver::synchronize-source-versions") != "none")
	{
		// don't do synchronization for removals
		if (supposedVersion)
		{
			__synchronize_related_packages(solution, supposedVersion, true);
		}
	}

	solution->pendingAction.reset();
}

bool __version_has_relation_expression(const shared_ptr< const BinaryVersion >& version,
		BinaryVersion::RelationTypes::Type dependencyType,
		const RelationExpression& relationExpression)
{
	auto relationExpressionString = relationExpression.getHashString();
	if (!relationExpressionString.empty())
	{
		const RelationLine& relationLine = version->relations[dependencyType];
		FORIT(candidateRelationExpressionIt, relationLine)
		{
			auto candidateString = candidateRelationExpressionIt->getHashString();
			if (!candidateString.empty() && relationExpressionString == candidateString)
			{
				return true;
			}
		}
	}
	return false;
}

void NativeResolverImpl::__add_actions_to_modify_package_entry(vector< Action >& actions,
		const string& packageName, const PackageEntry* packageEntry,
		BinaryVersion::RelationTypes::Type dependencyType, const RelationExpression& relationExpression,
		const vector< shared_ptr< const BinaryVersion > >& satisfyingVersions, bool tryHard)
{
	if (!packageEntry->sticked)
	{
		const shared_ptr< const BinaryVersion >& version = packageEntry->version;
		// change version of the package
		auto package = __cache->getBinaryPackage(packageName);
		auto versions = package->getVersions();
		FORIT(otherVersionIt, versions)
		{
			const shared_ptr< const BinaryVersion >& otherVersion = *otherVersionIt;

			// don't try existing version
			if (otherVersion->versionString == version->versionString)
			{
				continue;
			}

			bool found = false;
			if (tryHard)
			{
				// let's check if other version has the same relation
				// if it has, other version will also fail so it seems there is no sense trying it
				found = __version_has_relation_expression(otherVersion,
						dependencyType, relationExpression);
				if (!found)
				{
					// let's try even harder to find if the other version is really appropriate for us
					const RelationLine& relationLine = otherVersion->relations[dependencyType];
					FORIT(it, relationLine)
					{
						/* we check only relations from dependency group that caused
						   missing depends, it's not a full check, but pretty reasonable for
						   most cases; in rare cases that some problematic dependency
						   migrated to other dependency group, it will be revealed at
						   next check run

						   fail reveals that no one of available versions of dependent
						   packages can satisfy the main package, so if some relation's
						   satisfying versions are subset of failed ones, the version won't
						   be accepted as a resolution
						*/
						bool isMoreWide = false;
						auto candidateSatisfyingVersions = __cache->getSatisfyingVersions(*it);

						FORIT(candidateIt, candidateSatisfyingVersions)
						{
							auto predicate = bind2nd(PointerEqual< const BinaryVersion >(), *candidateIt);
							bool candidateNotFound = (std::find_if(satisfyingVersions.begin(), satisfyingVersions.end(),
									predicate) == satisfyingVersions.end());

							if (candidateNotFound)
							{
								// more wide relation, can't say nothing bad with it at time being
								isMoreWide = true;
								break;
							}
						}

						found = !isMoreWide;
						if (found)
						{
							break;
						}
					}
				}
			}
			if (!found)
			{
				// other version seems to be ok
				Action action;
				action.packageName = packageName;
				action.version = otherVersion;

				actions.push_back(std::move(action));
			}
		}

		if (__can_package_be_removed(packageName))
		{
			// remove the package
			Action action;
			action.packageName = packageName;

			actions.push_back(std::move(action));
		}
	}
}

void NativeResolverImpl::__add_actions_to_fix_dependency(vector< Action >& actions,
		const shared_ptr< Solution >& solution,
		const vector< shared_ptr< const BinaryVersion > >& satisfyingVersions)
{
	// install one of versions package needs
	FORIT(satisfyingVersionIt, satisfyingVersions)
	{
		const shared_ptr< const BinaryVersion >& satisfyingVersion = *satisfyingVersionIt;
		const string& satisfyingPackageName = satisfyingVersion->packageName;

		// can the package be updated?
		auto satisfyingPackageEntry = solution->getPackageEntry(satisfyingPackageName);
		if (!satisfyingPackageEntry || !satisfyingPackageEntry->sticked)
		{
			Action action;
			action.packageName = satisfyingPackageName;
			action.version = satisfyingVersion;

			actions.push_back(std::move(action));
		}
	}

}

void NativeResolverImpl::__prepare_stick_requests(vector< Action >& actions) const
{
	// each next action receives one more additional stick request to not
	// interfere with all previous solutions
	vector< string > packageNames;
	FORIT(actionIt, actions)
	{
		actionIt->packageToStickNames = packageNames;
		packageNames.push_back(actionIt->packageName);
	}
}

Resolver::UserAnswer::Type NativeResolverImpl::__propose_solution(
		const shared_ptr< Solution >& solution, Resolver::CallbackType callback)
{
	static const Resolver::SuggestedPackage emptySuggestedPackage;

	// build "user-frienly" version of solution
	Resolver::SuggestedPackages suggestedPackages;

	auto packageNames = solution->getPackageNames();
	FORIT(packageNameIt, packageNames)
	{
		const string& packageName = *packageNameIt;
		if (packageName == __dummy_package_name)
		{
			continue;
		}

		auto packageEntry = solution->getPackageEntry(packageName);
		auto it = suggestedPackages.insert(make_pair(packageName, emptySuggestedPackage)).first; // iterator of inserted element
		Resolver::SuggestedPackage& suggestedPackage = it->second;

		suggestedPackage.version = packageEntry->version;
		suggestedPackage.reasons = packageEntry->reasons;
		suggestedPackage.manuallySelected = __manually_modified_package_names.count(packageName);
	}

	// suggest found solution
	bool debugging = __config->getBool("debug::resolver");
	if (debugging)
	{
		__mydebug_wrapper(solution, "proposing this solution");
	}

	auto userAnswer = callback(suggestedPackages);
	if (debugging)
	{
		if (userAnswer == Resolver::UserAnswer::Accept)
		{
			__mydebug_wrapper(solution, "accepted");
		}
		else if (userAnswer == Resolver::UserAnswer::Decline)
		{
			__mydebug_wrapper(solution, "declined");
		}
	}

	return userAnswer;
}

vector< NativeResolverImpl::Action > NativeResolverImpl::__filter_unsynchronizeable_actions(
		const shared_ptr< Solution >& solution, const vector< Action >& actions)
{
	vector< Action > result;

	bool debugging = __config->getBool("debug::resolver");
	const bool trackReasons = __config->getBool("cupt::resolver::track-reasons");
	FORIT(actionIt, actions)
	{
		const shared_ptr< const BinaryVersion >& version = actionIt->version;
		if (!version || __can_related_packages_be_synchronized(solution, version))
		{
			result.push_back(*actionIt);
		}
		else
		{
			// we cannot proceed with it, so try deleting related packages
			auto unsynchronizeablePackageNames = __get_unsynchronizeable_related_package_names(
					solution, version);
			FORIT(unsynchronizeablePackageNameIt, unsynchronizeablePackageNames)
			{
				const string& unsynchronizeablePackageName = *unsynchronizeablePackageNameIt;
				if (solution->getPackageEntry(unsynchronizeablePackageName)->sticked)
				{
					continue;
				}

				bool foundSameAction = false;
				FORIT(resultActionIt, result)
				{
					if (resultActionIt->packageName == unsynchronizeablePackageName &&
							!resultActionIt->version)
					{
						foundSameAction = true;
						break;
					}
				}

				if (!foundSameAction)
				{
					Action action;
					action.packageName = unsynchronizeablePackageName;
					if (trackReasons)
					{
						action.reason.reset(new SynchronizationReason(version->packageName));
					}
					result.push_back(action);
				}
			}
			if (debugging)
			{
				__mydebug_wrapper(solution, sf(
						"cannot consider installing %s %s: unable to synchronize related packages (%s)",
						version->packageName.c_str(), version->versionString.c_str(),
						join(", ", unsynchronizeablePackageNames).c_str()));
			}
		}
	}
	return result;
}

bool NativeResolverImpl::__is_soft_dependency_ignored(const shared_ptr< const BinaryVersion >& version,
		BinaryVersion::RelationTypes::Type dependencyType,
		const RelationExpression& relationExpression,
		const vector< shared_ptr< const BinaryVersion > >& satisfyingVersions) const
{
	auto wasSatisfiedInPast = __is_version_array_intersects_with_packages(
				satisfyingVersions, __old_solution);
	if (wasSatisfiedInPast)
	{
		return false;
	}

	if (dependencyType == BinaryVersion::RelationTypes::Recommends)
	{
		if (!__config->getBool("apt::install-recommends"))
		{
			return true;
		}
	}
	else // Suggests
	{
		if (!__config->getBool("apt::install-suggests"))
		{
			return true;
		}
	}

	auto oldPackageEntry = __old_solution->getPackageEntry(version->packageName);
	if (oldPackageEntry)
	{
		const shared_ptr< const BinaryVersion >& oldVersion = oldPackageEntry->version;
		if (oldVersion && __version_has_relation_expression(oldVersion,
			dependencyType, relationExpression))
		{
			// the fact that we are here means that the old version of this package
			// had exactly the same relation expression, and it was unsatisfied
			// so, upgrading the version doesn't bring anything new
			return true;
		}
	}

	return false;
}

void __get_sorted_package_names(const vector< const string* >& source,
		const map< string, size_t >& failCounts, vector< const string* >& result)
{
	// use Schwarz transformation
	struct ForSort
	{
		const string* packageNamePtr;
		size_t failCount;
	};

	vector< ForSort > aux;
	aux.reserve(source.size());
	FORIT(packageNamePtrIt, source)
	{
		const string& packageName = **packageNamePtrIt;

		auto failCountIt = failCounts.find(packageName);
		auto failCount = (failCountIt != failCounts.end() ? failCountIt->second : 0);

		aux.push_back(ForSort { &packageName, failCount });
	}

	std::stable_sort(aux.begin(), aux.end(),
			[](const ForSort& left, const ForSort& right) -> bool
			{
				return (left.failCount > right.failCount);
			});

	FORIT(auxIt, aux)
	{
		result.push_back(auxIt->packageNamePtr);
	}
}

bool NativeResolverImpl::__verify_relation_line(const shared_ptr< Solution >& solution,
		const string* packageNamePtr, const PackageEntry* packageEntry,
		const shared_ptr< const BinaryVersion >& version,
		BinaryVersion::RelationTypes::Type dependencyType, bool isDependencyAnti,
		BrokenDependencyInfo* bdi)
{
	const string* ignorePackageNamePtr = (isDependencyAnti ? packageNamePtr : NULL);

	const RelationLine& relationLine = version->relations[dependencyType];
	FORIT(relationExpressionIt, relationLine)
	{
		const RelationExpression& relationExpression = *relationExpressionIt;

		bdi->satisfyingVersions = __cache->getSatisfyingVersions(relationExpression);
		bdi->intersectVersionPtr = __is_version_array_intersects_with_packages(
				bdi->satisfyingVersions, solution, ignorePackageNamePtr);

		// check if the relation expression satisfied/unsatisfied respectively
		if (isDependencyAnti == !bdi->intersectVersionPtr) // XOR
		{
			continue;
		}

		if (dependencyType == BinaryVersion::RelationTypes::Recommends ||
			dependencyType == BinaryVersion::RelationTypes::Suggests)
		{
			// this is a soft dependency

			const RelationLine& fakelySatisfied = packageEntry->fakelySatisfied;
			if (std::find(fakelySatisfied.begin(), fakelySatisfied.end(), relationExpression) !=
				fakelySatisfied.end())
			{
				// this soft relation expression was already fakely satisfied (score penalty)
				continue;
			}

			if (__is_soft_dependency_ignored(version, dependencyType,
					relationExpression, bdi->satisfyingVersions))
			{
				continue;
			}
		}

		bdi->relationExpressionPtr = &*relationExpressionIt;
		return false;
	}
	return true;
}

void NativeResolverImpl::__generate_possible_actions(vector< Action >* possibleActionsPtr,
		const shared_ptr< Solution >& solution, const string& packageName,
		const PackageEntry* packageEntry, const shared_ptr< const BinaryVersion >& version,
		const BrokenDependencyInfo& bdi,
		BinaryVersion::RelationTypes::Type dependencyType, bool isDependencyAnti)
{
	vector< Action >& possibleActions = *possibleActionsPtr;
	const RelationExpression& failedRelationExpression = *(bdi.relationExpressionPtr);

	if (!isDependencyAnti)
	{
		if (dependencyType == BinaryVersion::RelationTypes::Recommends ||
			dependencyType == BinaryVersion::RelationTypes::Suggests)
		{
			// ok, then we have one more possible solution - do nothing at all
			Action action;
			action.packageName = packageName;
			action.version = version;
			// set profit manually, as we are inserting fake action here
			action.profit = (dependencyType == BinaryVersion::RelationTypes::Recommends ? -200 : -50);
			action.fakelySatisfies.reset(new RelationExpression(*(bdi.relationExpressionPtr)));

			possibleActions.push_back(std::move(action));
		}

		// also
		__add_actions_to_fix_dependency(possibleActions, solution, bdi.satisfyingVersions);
		__add_actions_to_modify_package_entry(possibleActions, packageName,
				packageEntry, dependencyType, failedRelationExpression, bdi.satisfyingVersions, true);
	}
	else
	{
		const shared_ptr< const BinaryVersion >& satisfyingVersion = *(bdi.intersectVersionPtr);
		const string& satisfyingPackageName = satisfyingVersion->packageName;
		auto satisfyingPackageEntry = solution->getPackageEntry(satisfyingPackageName);

		__add_actions_to_modify_package_entry(possibleActions, satisfyingPackageName,
				satisfyingPackageEntry, dependencyType, failedRelationExpression, bdi.satisfyingVersions);
		__add_actions_to_modify_package_entry(possibleActions, packageName,
				packageEntry, dependencyType, failedRelationExpression, bdi.satisfyingVersions);
	}
}

bool NativeResolverImpl::resolve(Resolver::CallbackType callback)
{
	auto solutionChooser = __select_solution_chooser();
	bool debugging = __config->getBool("debug::resolver");
	if (debugging)
	{
		debug("started resolving");
	}
	__require_strict_relation_expressions();

	const bool trackReasons = __config->getBool("cupt::resolver::track-reasons");

	auto dependencyGroups = __get_dependency_groups();

	list< shared_ptr< Solution > > solutions =
			{ __solution_storage.cloneSolution(__initial_solution) };
	(*solutions.begin())->prepare();

	// for each package entry 'count' will contain the number of failures
	// during processing these packages
	map< string, size_t > failCounts;

	bool checkFailed;

	while (!solutions.empty())
	{
		vector< Action > possibleActions;

		// choosing the solution to process
		auto currentSolutionIt = solutionChooser(solutions);
		const shared_ptr< Solution >& currentSolution = *currentSolutionIt;
		if (currentSolution->pendingAction)
		{
			currentSolution->prepare();
			__post_apply_action(currentSolution);
		}

		// for the speed reasons, we will correct one-solution problems directly in MAIN_LOOP
		// so, when an intermediate problem was solved, maybe it breaks packages
		// we have checked earlier in the loop, so we schedule a recheck
		//
		// once two or more solutions are available, loop will be ended immediately
		bool recheckNeeded = true;
		// first pass is performed in validate-only mode - skip all invalid packages
		// to decrease the number of valid packages not being validated
		bool validateOnlyPass = true;
		while (recheckNeeded)
		{
			recheckNeeded = false;
			checkFailed = false;

			FORIT(dependencyGroupIt, dependencyGroups)
			{
				auto dependencyType = dependencyGroupIt->type;
				auto isDependencyAnti = dependencyGroupIt->isAnti;

				/* to speed up the complex decision steps, if solution stack is not
				   empty, firstly check the packages that had a problem

				   we call MostlyUnchecked for filtering to process less packages in
				   __get_sorted_package_names; still, some returned packages may be
				   already checked */
				vector< const string* > packageNamePtrs;
				{
					auto source = currentSolution->getMostlyUncheckedPackageNames(dependencyType);
					__get_sorted_package_names(source, failCounts, packageNamePtrs);
				}

				FORIT(packageNamePtrIt, packageNamePtrs)
				{
					const string& packageName = **packageNamePtrIt;

					redo_package:

					auto packageEntry = currentSolution->setPackageEntryIfExists(packageName);

					// skip check if already marked as checked
					if (packageEntry->checked.test(dependencyType))
					{
						continue;
					}

					const shared_ptr< const BinaryVersion >& version = packageEntry->version;
					if (!version)
					{
						continue;
					}

					BrokenDependencyInfo brokenDependencyInfo;
					checkFailed = !__verify_relation_line(currentSolution, *packageNamePtrIt,
							packageEntry, version, dependencyType, isDependencyAnti,
							/* out -> */ &brokenDependencyInfo);
					if (checkFailed)
					{
						if (validateOnlyPass)
						{
							// do nothing, continue checking
							if (!possibleActions.empty())
							{
								fatal("internal error: some actions were generated in validate-only pass");
							}
							recheckNeeded = true;
							checkFailed = false;

							goto next_package;
						}
						__generate_possible_actions(&possibleActions, currentSolution, packageName,
								packageEntry, version, brokenDependencyInfo, dependencyType, isDependencyAnti);

						const RelationExpression& failedRelationExpression = *(brokenDependencyInfo.relationExpressionPtr);
						if (trackReasons)
						{
							// setting a reason
							shared_ptr< const Reason > reason(
									new RelationExpressionReason(version, dependencyType, failedRelationExpression));
							FORIT(possibleActionIt, possibleActions)
							{
								possibleActionIt->reason = reason;
							}
						}

						// mark package as failed one more time
						failCounts[packageName] += 1;

						if (debugging)
						{
							const char* satisfyState = (isDependencyAnti ? "satisfied" : "unsatisfied");
							auto message = sf("problem: package '%s': %s %s '%s'", packageName.c_str(), satisfyState,
									BinaryVersion::RelationTypes::rawStrings[dependencyType],
									failedRelationExpression.toString().c_str());
							__mydebug_wrapper(currentSolution, message);
						}
						if (possibleActions.size() == 1)
						{
							__pre_apply_action(currentSolution, currentSolution, possibleActions[0]);
							__post_apply_action(currentSolution);
							possibleActions.clear();

							recheckNeeded = true;
							checkFailed = false;
							goto redo_package;
						}
						goto finish_main_loop;
					}
					packageEntry->checked.set(dependencyType); // validate

					next_package:
					;
				}
			}
			validateOnlyPass = false;
		}
		finish_main_loop:

		if (!checkFailed)
		{
			// if the solution was only just finished
			if (!currentSolution->finished)
			{
				if (debugging)
				{
					__mydebug_wrapper(currentSolution, "finished");
				}
				currentSolution->finished = 1;

				// clean up automatically installed by resolver and now unneeded packages
				__clean_automatically_installed(currentSolution);

				/* now, as we use partial checks (using set_package_entry/validate), before
				   we present a solution it's a good idea to validate it from scratch finally:
				   if it ever turns that partial checks pass a wrong solution, we must catch it

				   so, we schedule a last check round for a solution, but as it already has
				   'finished' property set, if the problem will appear, _pre_apply_action will
				   die loudly
				*/
				auto packageNames = currentSolution->getPackageNames();
				FORIT(packageNameIt, packageNames)
				{
					// invalidating all package entries
					__solution_storage.setPackageEntry(currentSolution, *packageNameIt);
				}
				continue;
			}

			// resolver can refuse the solution
			auto newSelectedSolution = solutionChooser(solutions);
			if (newSelectedSolution != currentSolutionIt)
			{
				continue; // ok, process other solution
			}

			auto userAnswer = __propose_solution(currentSolution, callback);
			switch (userAnswer)
			{
				case Resolver::UserAnswer::Accept:
					// yeah, this is end of our tortures
					return true;
				case Resolver::UserAnswer::Abandon:
					// user has selected abandoning all further efforts
					return false;
				case Resolver::UserAnswer::Decline:
					// caller hasn't accepted this solution, well, go next...

					// purge current solution
					solutions.erase(currentSolutionIt);
			}
		}
		else
		{
			if (__config->getString("cupt::resolver::synchronize-source-versions") == "hard")
			{
				// if we have to synchronize source versions, can related packages be updated too?
				// filter out actions that don't match this criteria
				possibleActions = __filter_unsynchronizeable_actions(
						currentSolution, possibleActions);
			}

			__prepare_stick_requests(possibleActions);

			if (!possibleActions.empty())
			{
				__calculate_profits(currentSolution, possibleActions);
				__pre_apply_actions_to_solution_tree(solutions, currentSolution, possibleActions);
			}
			else
			{
				if (debugging)
				{
					__mydebug_wrapper(currentSolution, "no solutions");
				}
			}

			// purge current solution
			solutions.erase(currentSolutionIt);

			if (!possibleActions.empty())
			{
				// some new solutions were added
				__erase_worst_solutions(solutions);
			}
		}
	}
	// no solutions pending, we have a great fail
	return false;
}

const string NativeResolverImpl::__dummy_package_name = "dummy_package_name";

}
}

