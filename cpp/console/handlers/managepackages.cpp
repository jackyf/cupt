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
#include <iostream>
using std::cout;
using std::endl;

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../common.hpp"
#include "../handlers.hpp"
#include "../misc.hpp"
#include "../selectors.hpp"

#include <cupt/system/state.hpp>
#include <cupt/system/resolver.hpp>
#include <cupt/system/resolvers/native.hpp>
#include <cupt/system/snapshots.hpp>
#include <cupt/file.hpp>
#include <cupt/system/worker.hpp>
#include <cupt/cache/sourceversion.hpp>

typedef Worker::Action WA;

static void preProcessMode(ManagePackages::Mode& mode, const shared_ptr< Config >& config,
		Resolver& resolver)
{
	if (mode == ManagePackages::FullUpgrade || mode == ManagePackages::SafeUpgrade)
	{
		if (mode == ManagePackages::SafeUpgrade)
		{
			config->setScalar("cupt::resolver::no-remove", "yes");
		}
		resolver.upgrade();

		// despite the main action is {safe,full}-upgrade, allow package
		// modifiers in the command line just as with the install command
		mode = ManagePackages::Install;
	}
	else if (mode == ManagePackages::Purge)
	{
		mode = ManagePackages::Remove;
		config->setScalar("cupt::worker::purge", "yes");
	}
	else if (mode == ManagePackages::Satisfy || mode == ManagePackages::BuildDepends)
	{
		config->setScalar("apt::install-recommends", "no");
		config->setScalar("apt::install-suggests", "no");
	}
	else if (mode == ManagePackages::BuildDepends)
	{
		resolver.satisfyRelationExpression(RelationExpression("build-essential"));
	}
}

static void unrollFileArguments(vector< string >& arguments)
{
	vector< string > newArguments;
	FORIT(argumentIt, arguments)
	{
		const string& argument = *argumentIt;
		if (!argument.empty() && argument[0] == '@')
		{
			const string path = argument.substr(1);
			// reading package expressions from file
			string openError;
			File file(path, "r", openError);
			if (!openError.empty())
			{
				fatal("unable to open file '%s': %s", path.c_str(), openError.c_str());
			}
			string line;
			while (!file.getLine(line).eof())
			{
				newArguments.push_back(line);
			}
		}
		else
		{
			newArguments.push_back(argument);
		}
	}
	arguments.swap(newArguments);
}

void __satisfy_or_unsatisfy(Resolver& resolver,
		const RelationLine& relationLine, bool negative)
{
	FORIT(relationExpressionIt, relationLine)
	{
		if (negative)
		{
			resolver.unsatisfyRelationExpression(*relationExpressionIt);
		}
		else
		{
			resolver.satisfyRelationExpression(*relationExpressionIt);
		}
	}
}

static void processSatisfyExpression(const shared_ptr< Config >& config,
		Resolver& resolver, string packageExpression)
{
	bool negative = false;
	if (!packageExpression.empty() && *(packageExpression.rbegin()) == '-')
	{
		negative = true;
		packageExpression.erase(packageExpression.end() - 1);
	}

	auto relationLine = ArchitecturedRelationLine(packageExpression)
			.toRelationLine(config->getString("apt::architecture"));

	__satisfy_or_unsatisfy(resolver, relationLine, negative);
}

static void processBuildDependsExpression(const shared_ptr< Config >& config,
		const shared_ptr< const Cache >& cache,
		Resolver& resolver, const string& packageExpression)
{
	auto architecture = config->getString("apt::architecture");

	auto versions = selectSourceVersionsWildcarded(cache, packageExpression);

	FORIT(versionIt, versions)
	{
		const shared_ptr< const SourceVersion >& version = *versionIt;
		__satisfy_or_unsatisfy(resolver, version->relations[SourceVersion::RelationTypes::BuildDepends]
				.toRelationLine(architecture), false);
		__satisfy_or_unsatisfy(resolver, version->relations[SourceVersion::RelationTypes::BuildDependsIndep]
				.toRelationLine(architecture), false);
		__satisfy_or_unsatisfy(resolver, version->relations[SourceVersion::RelationTypes::BuildConflicts]
				.toRelationLine(architecture), true);
		__satisfy_or_unsatisfy(resolver, version->relations[SourceVersion::RelationTypes::BuildConflictsIndep]
				.toRelationLine(architecture), true);
	}
}

static void processPackageExpressions(const shared_ptr< Config >& config,
		const shared_ptr< const Cache >& cache, ManagePackages::Mode mode,
		Resolver& resolver, const vector< string >& packageExpressions)
{
	FORIT(packageExpressionIt, packageExpressions)
	{
		if (mode == ManagePackages::Satisfy)
		{
			processSatisfyExpression(config, resolver, *packageExpressionIt);
		}
		else if (mode == ManagePackages::BuildDepends)
		{
			processBuildDependsExpression(config, cache, resolver, *packageExpressionIt);
		}
		else
		{
			// localizing mode
			ManagePackages::Mode oldMode = mode;
			ManagePackages::Mode mode = oldMode;

			string packageExpression = *packageExpressionIt;

			auto versions = selectBinaryVersionsWildcarded(cache, packageExpression, false);
			if (versions.empty())
			{
				/* we have a funny situation with package names like 'g++',
				   where one don't know is there simple package name or '+'/'-'
				   modifier at the end of package name, so we enter here only if
				   it seems that there is no such binary package */

				// "localizing" action to make it modifiable by package modifiers
				if (!packageExpression.empty())
				{
					const char& lastLetter = *(packageExpression.end() - 1);
					if (lastLetter == '+')
					{
						mode = ManagePackages::Install;
						packageExpression.erase(packageExpression.end() - 1);
					}
					else if (lastLetter == '-')
					{
						mode = ManagePackages::Remove;
						packageExpression.erase(packageExpression.end() - 1);
					}
				}
			}

			if (mode == ManagePackages::Install)
			{
				if (versions.empty())
				{
					versions = selectBinaryVersionsWildcarded(cache, packageExpression);
				}
				FORIT(versionIt, versions)
				{
					resolver.installVersion(*versionIt);
				}
			}
			else // ManagePackages::Remove
			{
				if (versions.empty())
				{
					// retry, still non-fatal in non-wildcard mode, to deal with packages in 'config-files' state
					bool wildcardsPresent = packageExpression.find('?') != string::npos ||
							packageExpression.find('*') != string::npos;
					versions = selectBinaryVersionsWildcarded(cache, packageExpression, wildcardsPresent);
				}

				if (!versions.empty())
				{
					FORIT(versionIt, versions)
					{
						resolver.removePackage((*versionIt)->packageName);
					}
				}
				else
				{
					checkPackageName(packageExpression);
					if (!cache->getSystemState()->getInstalledInfo(packageExpression) &&
						!getBinaryPackage(cache, packageExpression, false))
					{
						fatal("unable to find binary package/expression '%s'", packageExpression.c_str());
					}

					resolver.removePackage(packageExpression);
				}
			}
		}
	}
}

void printUnpackedSizeChanges(const map< string, ssize_t >& unpackedSizesPreview)
{
	ssize_t totalUnpackedSizeChange = 0;
	FORIT(it, unpackedSizesPreview)
	{
		totalUnpackedSizeChange += it->second;
	}

	string message;
	if (totalUnpackedSizeChange >= 0)
	{
		message = sf(__("After unpacking %s will be used."),
				humanReadableSizeString(totalUnpackedSizeChange).c_str());
	} else {
		message = sf(__("After unpacking %s will be freed."),
				humanReadableSizeString(-totalUnpackedSizeChange).c_str());
	}

	cout << message << endl;
}

void printDownloadSizes(const pair< size_t, size_t >& downloadSizes)
{
	auto total = downloadSizes.first;
	auto need = downloadSizes.second;
	cout << sf(__("Need to get %s/%s of archives. "),
					humanReadableSizeString(need).c_str(),
					humanReadableSizeString(total).c_str());
}

void showVersion(const shared_ptr< const Cache >& cache, const string& packageName,
		const Resolver::SuggestedPackage& suggestedPackage)
{
	auto package = cache->getBinaryPackage(packageName);
	if (!package)
	{
		fatal("internal error: no binary package '%s' available", packageName.c_str());
	}
	auto oldVersion = package->getInstalledVersion();

	string oldVersionString = oldVersion ? oldVersion->versionString : "";

	auto newVersion = suggestedPackage.version;
	string newVersionString = newVersion ? newVersion->versionString : "";

	if (!oldVersionString.empty() && !newVersionString.empty())
	{
		cout << sf(" [%s -> %s]", oldVersionString.c_str(), newVersionString.c_str());
	}
	else if (!oldVersionString.empty())
	{
		cout << sf(" [%s]", oldVersionString.c_str());
	}
	else
	{
		cout << sf(" [%s]", newVersionString.c_str());
	}
}

void showSizeChange(ssize_t bytes)
{
	if (bytes != 0)
	{
		auto sizeChangeString = (bytes >= 0) ?
				string("+") + humanReadableSizeString(bytes) :
				string("-") + humanReadableSizeString(-bytes);
		cout << " <" << sizeChangeString << '>';
	}
}

void printPackageNamesByLine(const vector< string >& packageNames)
{
	cout << endl;
	FORIT(it, packageNames)
	{
		cout << *it << endl;
	}
	cout << endl;
}

void checkForUntrustedPackages(const shared_ptr< const Worker::ActionsPreview >& actionsPreview,
		bool& isDangerous)
{
	vector< string > untrustedPackageNames;
	// generate loud warning for unsigned versions
	static const WA::Type affectedActionTypes[] = { WA::Install, WA::Upgrade, WA::Downgrade };

	for (size_t i = 0; i < sizeof(affectedActionTypes) / sizeof(WA::Type); ++i)
	{
		const WA::Type& actionType = affectedActionTypes[i];
		const Resolver::SuggestedPackages& suggestedPackages = (*actionsPreview)[actionType];

		FORIT(it, suggestedPackages)
		{
			if (!(it->second.version->isVerified()))
			{
				untrustedPackageNames.push_back(it->first);
			}
		}
	}

	if (!untrustedPackageNames.empty())
	{
		isDangerous = true;
		cout << __("WARNING! The untrusted versions of the following packages will be used:") << endl;
		printPackageNamesByLine(untrustedPackageNames);
	}
}

void checkForRemovalOfEssentialPackages(const shared_ptr< const Cache >& cache,
		const shared_ptr< const Worker::ActionsPreview >& actionsPreview,
		bool& isDangerous)
{
	vector< string > essentialPackageNames;
	// generate loud warning for unsigned versions
	static const WA::Type affectedActionTypes[] = { WA::Remove, WA::Purge };

	for (size_t i = 0; i < sizeof(affectedActionTypes) / sizeof(WA::Type); ++i)
	{
		const WA::Type& actionType = affectedActionTypes[i];
		const Resolver::SuggestedPackages& suggestedPackages = (*actionsPreview)[actionType];

		FORIT(it, suggestedPackages)
		{
			const string& packageName = it->first;
			auto package = cache->getBinaryPackage(packageName);
			if (package)
			{
				auto version = package->getInstalledVersion();
				if (version) // may return false when purge of config-files package when candidates available
				{
					if (version->essential)
					{
						essentialPackageNames.push_back(packageName);
					}
				}
			}
		}
	}

	if (!essentialPackageNames.empty())
	{
		isDangerous = true;
		cout << __("WARNING! The following essential packages will be REMOVED:") << endl;
		printPackageNamesByLine(essentialPackageNames);
	}
}

void checkForIgnoredHolds(const shared_ptr< const Cache >& cache,
		const shared_ptr< const Worker::ActionsPreview >& actionsPreview,
		bool& isDangerous)
{
	vector< string > ignoredHoldsPackageNames;

	static const WA::Type affectedActionTypes[] = { WA::Install, WA::Upgrade,
			WA::Downgrade, WA::Remove, WA::Purge };
	for (size_t i = 0; i < sizeof(affectedActionTypes) / sizeof(WA::Type); ++i)
	{
		const WA::Type& actionType = affectedActionTypes[i];
		const Resolver::SuggestedPackages& suggestedPackages = (*actionsPreview)[actionType];

		FORIT(it, suggestedPackages)
		{
			const string& packageName = it->first;
			auto installedInfo = cache->getSystemState()->getInstalledInfo(packageName);
			if (installedInfo &&
				installedInfo->want == system::State::InstalledRecord::Want::Hold &&
				installedInfo->status != system::State::InstalledRecord::Status::ConfigFiles)
			{
				ignoredHoldsPackageNames.push_back(packageName);
			}
		}
	}
	if (!ignoredHoldsPackageNames.empty())
	{
		isDangerous = true;
		cout << __("WARNING! The following packages ON HOLD will change their state:") << endl;
		printPackageNamesByLine(ignoredHoldsPackageNames);
	}
}

void showReason(const Resolver::SuggestedPackage& suggestedPackage)
{
	static auto sayReason = [](const string& message)
	{
		cout << "  " << __("reason: ") << message << endl;
	};

	FORIT(reasonIt, suggestedPackage.reasons)
	{
		const shared_ptr< const Resolver::Reason >& reason = *reasonIt;
		if (auto r = dynamic_pointer_cast< const Resolver::RelationExpressionReason >(reason))
		{
			static const map< BinaryVersion::RelationTypes::Type, string > dependencyTypeTranslations = {
				{ BinaryVersion::RelationTypes::PreDepends, __("pre-depends on") },
				{ BinaryVersion::RelationTypes::Depends, __("depends on") },
				{ BinaryVersion::RelationTypes::Recommends, __("recommends") },
				{ BinaryVersion::RelationTypes::Suggests, __("suggests") },
				{ BinaryVersion::RelationTypes::Conflicts, __("conflicts with") },
				{ BinaryVersion::RelationTypes::Breaks, __("breaks") },
			};

			auto dependencyTypeTranslationIt = dependencyTypeTranslations.find(r->dependencyType);
			if (dependencyTypeTranslationIt == dependencyTypeTranslations.end())
			{
				warn("unsupported reason dependency type '%s'",
						BinaryVersion::RelationTypes::strings[r->dependencyType].c_str());
			}
			else
			{
				sayReason(sf("%s %s %s '%s'",
						r->version->packageName.c_str(), r->version->versionString.c_str(),
						dependencyTypeTranslationIt->second.c_str(),
						r->relationExpression.toString().c_str()));
			}
		}
		else if (auto r = dynamic_pointer_cast< const Resolver::SynchronizationReason >(reason))
		{
			sayReason(sf(__("synchronized with package '%s'"), r->packageName.c_str()));
		}
		else if (dynamic_pointer_cast< const Resolver::UserReason >(reason))
		{
			sayReason(__("user request"));
		}
		else if (dynamic_pointer_cast< const Resolver::AutoRemovalReason >(reason))
		{
			sayReason(__("auto-removal"));
		}
		else
		{
			warn("unsupported reason type");
		}
	}
	cout << endl;
}

void showUnsatisfiedSoftDependencies(const shared_ptr< const Config >& config,
		const shared_ptr< const Cache >& cache,
		const Resolver::SuggestedPackages& suggestedPackages,
		const shared_ptr< const Worker::ActionsPreview >& actionsPreview)
{
	vector< BinaryVersion::RelationTypes::Type > affectedDependencyTypes;
	if (config->getBool("cupt::resolver::keep-recommends"))
	{
		affectedDependencyTypes.push_back(BinaryVersion::RelationTypes::Recommends);
	}
	if (config->getBool("cupt::resolver::keep-suggests"))
	{
		affectedDependencyTypes.push_back(BinaryVersion::RelationTypes::Suggests);
	}
	if (affectedDependencyTypes.empty())
	{
		return;
	}

	vector< shared_ptr< const BinaryVersion > > versionsToRemove;
	static const WA::Type affectedActionTypes[] = { WA::Remove, WA::Purge };
	for (size_t i = 0; i < sizeof(affectedActionTypes) / sizeof(WA::Type); ++i)
	{
		const WA::Type& actionType = affectedActionTypes[i];
		const Resolver::SuggestedPackages& actionSuggestedPackages = (*actionsPreview)[actionType];
		FORIT(it, actionSuggestedPackages)
		{
			auto package = cache->getBinaryPackage(it->first);
			// package or version may be undefined for packages in 'config-files' state
			if (!package)
			{
				continue;
			}
			auto version = package->getInstalledVersion();
			if (version)
			{
				versionsToRemove.push_back(version);
			}
		}
	}

	vector< string > messages;

	FORIT(suggestedEntryIt, suggestedPackages)
	{
		const string& packageName = suggestedEntryIt->first;
		const Resolver::SuggestedPackage& suggestedPackage = suggestedEntryIt->second;
		if (!suggestedPackage.version)
		{
			continue;
		}

		FORIT(dependencyTypeIt, affectedDependencyTypes)
		{
			const BinaryVersion::RelationTypes::Type& dependencyType = *dependencyTypeIt;

			FORIT(relationExpressionIt, suggestedPackage.version->relations[dependencyType])
			{
				auto satisfyingVersions = cache->getSatisfyingVersions(*relationExpressionIt);

				bool breakagePossible = false;
				FORIT(versionToRemoveIt, versionsToRemove)
				{
					auto predicate = bind2nd(PointerEqual< const BinaryVersion >(), *versionToRemoveIt);
					breakagePossible = (std::find_if(satisfyingVersions.begin(), satisfyingVersions.end(),
							predicate) != satisfyingVersions.end());
					if (breakagePossible)
					{
						break;
					}
				}

				if (breakagePossible)
				{
					bool isBroken = true;
					FORIT(satisfyingVersionIt, satisfyingVersions)
					{
						const shared_ptr< const BinaryVersion >& satisfyingVersion = *satisfyingVersionIt;
						const string& satisfyingPackageName = satisfyingVersion->packageName;

						auto relevantSuggestedPackageIt = suggestedPackages.find(satisfyingPackageName);
						if (relevantSuggestedPackageIt == suggestedPackages.end())
						{
							continue;
						}
						const shared_ptr< const BinaryVersion >& relevantVersion = relevantSuggestedPackageIt->second.version;
						if (relevantVersion && relevantVersion->versionString == satisfyingVersion->versionString)
						{
							isBroken = false;
							break;
						}
					}
					if (isBroken)
					{
						const string& versionString = suggestedPackage.version->versionString;
						messages.push_back(sf("%s %s %s %s", packageName.c_str(), versionString.c_str(),
								(dependencyType == BinaryVersion::RelationTypes::Recommends ? __("recommends") : __("suggests")).c_str(),
								relationExpressionIt->toString().c_str()));
					}
				}
			}
		}
	}
	if (!messages.empty())
	{
		cout << __("Leave the following dependencies unresolved:") << endl;
		cout << endl;
		FORIT(messageIt, messages)
		{
			cout << *messageIt << endl;
		}
		cout << endl;
	}
}

Resolver::CallbackType generateManagementPrompt(const shared_ptr< const Config >& config,
		const shared_ptr< const Cache >& cache, const shared_ptr< Worker >& worker,
		bool showVersions, bool showSizeChanges)
{
	auto result = [&config, &cache, &worker, showVersions, showSizeChanges]
			(const Resolver::SuggestedPackages& suggestedPackages) -> Resolver::UserAnswer::Type
	{
		auto showReasons = config->getBool("cupt::resolver::track-reasons");

		worker->setDesiredState(suggestedPackages);

		auto actionsPreview = worker->getActionsPreview();
		auto unpackedSizesPreview = worker->getUnpackedSizesPreview();

		size_t actionCount = 0;
		{ // print planned actions
			static const map< WA::Type, string > actionNames = {
				{ WA::Install, __("INSTALLED") },
				{ WA::Remove, __("REMOVED") },
				{ WA::Upgrade, __("UPGRADED") },
				{ WA::Purge, __("PURGED") },
				{ WA::Downgrade, __("DOWNGRADED") },
				{ WA::Configure, __("CONFIGURED") },
				{ WA::Deconfigure, __("DECONFIGURED") },
			};
			cout << endl;

			static const WA::Type actionTypesInOrder[] = { WA::Install, WA::Upgrade, WA::Remove,
					WA::Purge, WA::Downgrade, WA::Configure, WA::Deconfigure };

			for (size_t i = 0; i < sizeof(actionTypesInOrder) / sizeof(WA::Type); ++i)
			{
				const WA::Type& actionType = actionTypesInOrder[i];

				const Resolver::SuggestedPackages& actionSuggestedPackages = (*actionsPreview)[actionType];

				if (actionSuggestedPackages.empty())
				{
					continue; // don't print empty lists
				}

				const string& actionName = actionNames.find(actionType)->second;
				cout << sf(__("The following %u packages will be %s:"),
						actionSuggestedPackages.size(), actionName.c_str()) << endl << endl;

				FORIT(it, actionSuggestedPackages)
				{
					++actionCount;

					const string& packageName = it->first;
					cout << packageName;
					if (actionType == WA::Remove || actionType == WA::Purge)
					{
						if (cache->isAutomaticallyInstalled(packageName))
						{
							cout << "(a)";
						}
					}

					if (showVersions)
					{
						showVersion(cache, packageName, it->second);
					}

					if (showSizeChanges)
					{
						showSizeChange(unpackedSizesPreview[packageName]);
					}

					if (showVersions || showSizeChanges || showReasons)
					{
						cout << endl; // put newline
					}
					else
					{
						cout << ' '; // put a space between package names
					}

					if (showReasons)
					{
						showReason(it->second);
					}
				}
				if (!showVersions && !showSizeChanges && !showReasons)
				{
					cout << endl;
				}
				cout << endl;
			}
		};

		// nothing to do maybe?
		if (actionCount == 0)
		{
			return Resolver::UserAnswer::Accept;
		}

		showUnsatisfiedSoftDependencies(config, cache, suggestedPackages, actionsPreview);

		bool isDangerousAction = false;
		if (!config->getBool("cupt::console::allow-untrusted"))
		{
			checkForUntrustedPackages(actionsPreview, isDangerousAction);
		}
		checkForRemovalOfEssentialPackages(cache, actionsPreview, isDangerousAction);
		checkForIgnoredHolds(cache, actionsPreview, isDangerousAction);

		{ // print size estimations
			auto downloadSizesPreview = worker->getDownloadSizesPreview();
			printDownloadSizes(downloadSizesPreview);
			printUnpackedSizeChanges(unpackedSizesPreview);
		}

		static const string confirmationForDangerousAction = __("Yes, do as I say!");
		string question;
		if (isDangerousAction)
		{
			question = sf(__("Dangerous actions selected. Type '%s' if you want to continue, 'q' to exit, anything else to discard this solution:\n"),
					confirmationForDangerousAction.c_str());
		}
		else
		{
			question = __("Do you want to continue? [y/N/q] ");
		}
		string positiveAnswer = isDangerousAction ? confirmationForDangerousAction : "y";

		cout << question;
		string answer;
		bool interactive;

		if (config->getBool("cupt::console::assume-yes"))
		{
			interactive = false;
			answer = "y";
		}
		else
		{
			interactive = true;
			std::getline(std::cin, answer);
		}

		if (!std::cin)
		{
			return Resolver::UserAnswer::Abandon;
		}

		if (!isDangerousAction)
		{
			FORIT(it, answer)
			{
				*it = std::tolower(*it); // lowercasing
			}
		}

		// deciding
		if (answer == positiveAnswer)
		{
			return Resolver::UserAnswer::Accept;
		}
		else if (answer == "q")
		{
			return Resolver::UserAnswer::Abandon;
		}
		else if (interactive)
		{
			// user haven't chosen this solution, try next one
			cout << __("Resolving further... ") << endl;
			return Resolver::UserAnswer::Decline;
		}
		else
		{
			// non-interactive, abandon immediately
			return Resolver::UserAnswer::Abandon;
		}
	};

	return result;
}

void parseManagementOptions(Context& context, vector< string >& packageExpressions,
		bool& showVersions, bool& showSizeChanges)
{
	bpo::options_description options;
	options.add_options()
		("no-install-recommends,R", "")
		("no-remove", "")
		("no-auto-remove", "")
		("max-solution-count", bpo::value< string >())
		("resolver", bpo::value< string >())
		("show-versions,V", "")
		("show-size-changes,Z", "")
		("show-reasons,D", "")
		("show-deps", "")
		("download-only,d", "")
		("assume-yes", "")
		("yes,y", "");
	auto variables = parseOptions(context, options, packageExpressions);

	auto config = context.getConfig();
	if (variables.count("max-solution-count"))
	{
		config->setScalar("cupt::resolver::max-solution-count",
				variables["max-solution-count"].as<string>());
	}
	if (variables.count("resolver"))
	{
		config->setScalar("cupt::resolver::type", variables["resolver"].as<string>());
	}
	if (variables.count("assume-yes") || variables.count("yes"))
	{
		config->setScalar("cupt::console::assume-yes", "yes");
	}
	if (variables.count("show-reasons") || variables.count("show-deps"))
	{
		config->setScalar("cupt::resolver::track-reasons", "yes");
	}
	if (variables.count("no-install-recommends"))
	{
		config->setScalar("apt::install-recommends", "no");
	}
	if (variables.count("no-remove"))
	{
		config->setScalar("cupt::resolver::no-remove", "yes");
	}
	if (variables.count("download-only"))
	{
		config->setScalar("cupt::worker::download-only", "yes");
	}
	if (variables.count("no-auto-remove"))
	{
		config->setScalar("cupt::resolver::auto-remove", "no");
	}

	showVersions = variables.count("show-versions");
	showSizeChanges = variables.count("show-size-changes");
}

int managePackages(Context& context, ManagePackages::Mode mode)
{
	auto config = context.getConfig();

	// turn off info parsing, we don't need it
	if (!shellMode)
	{
		Version::parseInfoOnly = false;
	}

	Package::memoize = true;
	Cache::memoize = true;

	vector< string > packageExpressions;
	bool showVersions;
	bool showSizeChanges;
	parseManagementOptions(context, packageExpressions, showVersions, showSizeChanges);

	unrollFileArguments(packageExpressions);

	// snapshot handling
	string snapshotName; // empty if not applicable
	if (mode == ManagePackages::LoadSnapshot)
	{
		if (packageExpressions.size() != 1)
		{
			fatal("exactly one argument (the snapshot name) should be specified");
		}
		snapshotName = packageExpressions[0];
		packageExpressions.clear();
		{
			Snapshots snapshots(config);
			snapshots.setupConfigForSnapshotOnly(snapshotName);
		}
		mode = ManagePackages::Install;
	}

	shared_ptr< const Cache > cache;
	{
		vector< string > packageNameGlobsToReinstall;
		if (mode == ManagePackages::Reinstall)
		{
			mode = ManagePackages::Install;
			packageNameGlobsToReinstall = packageExpressions;
		}

		// source packages are needed for for synchronizing source versions
		bool buildSource = (mode == ManagePackages::BuildDepends ||
				config->getString("cupt::resolver::synchronize-source-versions") != "none");

		cout << __("Building the package cache... ") << endl;
		cache = context.getCache(buildSource, true, true, packageNameGlobsToReinstall);
	}

	cout << __("Initializing package resolver and worker... ") << endl;
	std::unique_ptr< Resolver > resolver;
	{
		if (!config->getString("cupt::resolver::external-command").empty())
		{
			fatal("using external resolver is not supported now");
		}
		else
		{
			resolver.reset(new NativeResolver(config, cache));
		}
	}

	if (!snapshotName.empty())
	{
		Snapshots snapshots(config);
		snapshots.setupResolverForSnapshotOnly(snapshotName, *cache, *resolver);
	}

	shared_ptr< Worker > worker(new Worker(config, cache));

	cout << __("Scheduling requested actions... ") << endl;

	preProcessMode(mode, config, *resolver);
	processPackageExpressions(config, cache, mode, *resolver, packageExpressions);

	cout << __("Resolving possible unmet dependencies... ") << endl;

	auto callback = generateManagementPrompt(config, cache, worker, showVersions, showSizeChanges);
	bool resolved = resolver->resolve(callback);

	// at this stage resolver has done its work, so to does not consume the RAM
	resolver.reset();

	if (resolved)
	{
		// if some solution was found and user has accepted it

		auto downloadProgress = getDownloadProgress(config);
		cout << __("Performing requested actions:") << endl;
		try
		{
			worker->changeSystem(downloadProgress);
		}
		catch (Exception&)
		{
			fatal("unable to do requested actions");
		}
		return 0;
	}
	else
	{
		cout << __("Abandoned or no more solutions.") << endl;
		return 1;
	}
}

int distUpgrade(Context& context)
{
	{ // 1st stage: upgrading of package management tools
		cout << __("[ upgrading package management tools ]") << endl;
		cout << endl;
		context.unparsed.push_back("dpkg");
		context.unparsed.push_back("cupt-experimental");
		if (managePackages(context, ManagePackages::Install) != 0)
		{
			fatal("upgrading of the package management tools failed");
		}
	}

	{ // 2nd stage: full upgrade
		cout << endl;
		cout << __("[ upgrading the system ]");
		cout << endl;
		auto argc = context.argc;
		auto argv = context.argv;
		for (int i = 0; i < argc; ++i)
		{
			if (!strcmp(argv[i], "dist-upgrade"))
			{
				strcpy(argv[i], "full-upgrade");
			}
		}
		execvp(argv[0], argv);
		fatal("upgrading the system failed: execvp failed: EEE");
	}
	return 0; // unreachable due to exec
}

int updateReleaseAndIndexData(Context& context)
{
	bpo::options_description noOptions;
	vector< string > arguments;
	auto variables = parseOptions(context, noOptions, arguments);
	checkNoExtraArguments(arguments);

	auto config = context.getConfig();

	auto cache = context.getCache(false, false, false);

	auto downloadProgress = getDownloadProgress(config);
	Worker worker(config, cache);

	// may throw exception
	worker.updateReleaseAndIndexData(downloadProgress);

	return 0;
}

int changeAutoInstalledState(Context& context, bool value)
{
	bpo::options_description noOptions;
	vector< string > arguments;
	auto variables = parseOptions(context, noOptions, arguments);

	auto config = context.getConfig();
	auto cache = context.getCache(false, false, false);

	Worker worker(config, cache);

	FORIT(packageNameIt, arguments)
	{
		worker.setAutomaticallyInstalledFlag(*packageNameIt, value);
	}

	return 0;
}

int cleanArchives(Context& context, bool leaveAvailable)
{
	if (!shellMode)
	{
		Version::parseInfoOnly = false;
		Version::parseRelations = false;
	}

	bpo::options_description noOptions;
	vector< string > arguments;
	auto variables = parseOptions(context, noOptions, arguments);
	checkNoExtraArguments(arguments);

	auto config = context.getConfig();
	auto cache = context.getCache(false, leaveAvailable, leaveAvailable);

	Worker worker(config, cache);
	uint64_t totalDeletedBytes = 0;

	auto info = worker.getArchivesInfo();
	FORIT(infoIt, info)
	{
		if (leaveAvailable && infoIt->second /* version is not empty */)
		{
			continue; // skip this one
		}

		const string& path = infoIt->first;

		struct stat stat_structure;
		if (lstat(path.c_str(), &stat_structure))
		{
			fatal("lstat() failed: '%s': EEE", path.c_str());
		}
		else
		{
			size_t size = stat_structure.st_size;
			totalDeletedBytes += size;
			cout << sf(__("Deleting '%s' <%s>..."), path.c_str(), humanReadableSizeString(size).c_str()) << endl;
			worker.deleteArchive(path);
		}
	}
	cout << sf(__("Freed %s of disk space."), humanReadableSizeString(totalDeletedBytes).c_str()) << endl;
	return 0;
}

