/**************************************************************************
*   Copyright (C) 2010-2011 by Eugene V. Lyubimkin                        *
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
const WA::Type fakeNotPolicyVersionAction = WA::Type(999);


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
				fatal2("unable to open file '%s': %s", path, openError);
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
		const RelationLine& relationLine, ManagePackages::Mode mode)
{
	FORIT(relationExpressionIt, relationLine)
	{
		if (mode == ManagePackages::Unsatisfy)
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
		Resolver& resolver, string packageExpression, ManagePackages::Mode mode)
{
	if (mode == ManagePackages::Satisfy && !packageExpression.empty() && *(packageExpression.rbegin()) == '-')
	{
		mode = ManagePackages::Unsatisfy;
		packageExpression.erase(packageExpression.end() - 1);
	}

	auto relationLine = ArchitecturedRelationLine(packageExpression)
			.toRelationLine(config->getString("apt::architecture"));

	__satisfy_or_unsatisfy(resolver, relationLine, mode);
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
				.toRelationLine(architecture), ManagePackages::Satisfy);
		__satisfy_or_unsatisfy(resolver, version->relations[SourceVersion::RelationTypes::BuildDependsIndep]
				.toRelationLine(architecture), ManagePackages::Satisfy);
		__satisfy_or_unsatisfy(resolver, version->relations[SourceVersion::RelationTypes::BuildConflicts]
				.toRelationLine(architecture), ManagePackages::Unsatisfy);
		__satisfy_or_unsatisfy(resolver, version->relations[SourceVersion::RelationTypes::BuildConflictsIndep]
				.toRelationLine(architecture), ManagePackages::Unsatisfy);
	}
}

static void processInstallOrRemoveExpression(const shared_ptr< const Cache >& cache,
		Resolver& resolver, ManagePackages::Mode mode, string packageExpression,
		set< string >& purgedPackageNames)
{
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
	else // ManagePackages::Remove or ManagePackages::Purge
	{
		if (versions.empty())
		{
			// retry, still non-fatal in non-wildcard mode, to deal with packages in 'config-files' state
			bool wildcardsPresent = packageExpression.find('?') != string::npos ||
					packageExpression.find('*') != string::npos;
			versions = selectBinaryVersionsWildcarded(cache, packageExpression, wildcardsPresent);
		}

		auto scheduleRemoval = [&resolver, &purgedPackageNames, mode](const string& packageName)
		{
			resolver.removePackage(packageName);
			if (mode == ManagePackages::Purge)
			{
				purgedPackageNames.insert(packageName);
			}
		};

		if (!versions.empty())
		{
			FORIT(versionIt, versions)
			{
				scheduleRemoval((*versionIt)->packageName);
			}
		}
		else
		{
			checkPackageName(packageExpression);
			if (!cache->getSystemState()->getInstalledInfo(packageExpression) &&
				!getBinaryPackage(cache, packageExpression, false))
			{
				fatal2("unable to find binary package/expression '%s'", packageExpression);
			}

			scheduleRemoval(packageExpression);
		}
	}
}

static void processPackageExpressions(const shared_ptr< Config >& config,
		const shared_ptr< const Cache >& cache, ManagePackages::Mode& mode,
		Resolver& resolver, const vector< string >& packageExpressions,
		set< string >& purgedPackageNames)
{
	FORIT(packageExpressionIt, packageExpressions)
	{
		if (*packageExpressionIt == "--remove")
		{
			mode = ManagePackages::Remove;
		}
		else if (*packageExpressionIt == "--purge")
		{
			mode = ManagePackages::Purge;
		}
		else if (*packageExpressionIt == "--install")
		{
			mode = ManagePackages::Install;
		}
		else if (*packageExpressionIt == "--satisfy")
		{
			mode = ManagePackages::Satisfy;
		}
		else if (*packageExpressionIt == "--unsatisfy")
		{
			mode = ManagePackages::Unsatisfy;
		}
		else if (mode == ManagePackages::Satisfy || mode == ManagePackages::Unsatisfy)
		{
			processSatisfyExpression(config, resolver, *packageExpressionIt, mode);
		}
		else if (mode == ManagePackages::BuildDepends)
		{
			processBuildDependsExpression(config, cache, resolver, *packageExpressionIt);
		}
		else
		{
			processInstallOrRemoveExpression(cache, resolver, mode,
					*packageExpressionIt, purgedPackageNames);
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
		message = format2(__("After unpacking %s will be used."),
				humanReadableSizeString(totalUnpackedSizeChange));
	} else {
		message = format2(__("After unpacking %s will be freed."),
				humanReadableSizeString(-totalUnpackedSizeChange));
	}

	cout << message << endl;
}

void printDownloadSizes(const pair< size_t, size_t >& downloadSizes)
{
	auto total = downloadSizes.first;
	auto need = downloadSizes.second;
	cout << format2(__("Need to get %s/%s of archives. "),
			humanReadableSizeString(need), humanReadableSizeString(total));
}

void showVersion(const shared_ptr< const Cache >& cache, const string& packageName,
		const Resolver::SuggestedPackage& suggestedPackage, WA::Type actionType)
{
	auto package = cache->getBinaryPackage(packageName);
	if (!package)
	{
		fatal2("internal error: no binary package '%s' available", packageName);
	}
	auto oldVersion = package->getInstalledVersion();

	string oldVersionString = oldVersion ? oldVersion->versionString : "";

	auto newVersion = suggestedPackage.version;
	string newVersionString = newVersion ? newVersion->versionString : "";

	if (!oldVersionString.empty() && !newVersionString.empty() &&
			(actionType != fakeNotPolicyVersionAction || oldVersionString != newVersionString))
	{
		cout << format2(" [%s -> %s]", oldVersionString, newVersionString);
	}
	else if (!oldVersionString.empty())
	{
		cout << format2(" [%s]", oldVersionString);
	}
	else
	{
		cout << format2(" [%s]", newVersionString);
	}

	if (actionType == fakeNotPolicyVersionAction)
	{
		cout << ", " << __("preferred") << ": " << cache->getPolicyVersion(package)->versionString;
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
		const Resolver::SuggestedPackages& suggestedPackages = actionsPreview->groups[actionType];

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
		const Resolver::SuggestedPackages& suggestedPackages = actionsPreview->groups[actionType];

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
		const Resolver::SuggestedPackages& suggestedPackages = actionsPreview->groups[actionType];

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
	FORIT(reasonIt, suggestedPackage.reasons)
	{
		cout << "  " << __("reason: ") << (*reasonIt)->toString() << endl;
	}
	cout << endl;
}

void showUnsatisfiedSoftDependencies(const Resolver::Offer& offer, std::stringstream* summaryStreamPtr)
{
	vector< string > messages;
	FORIT(unresolvedProblemIt, offer.unresolvedProblems)
	{
		messages.push_back((*unresolvedProblemIt)->toString());
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

		*summaryStreamPtr << format2(__("  %u dependency problems will stay unresolved"),
				offer.unresolvedProblems.size()) << endl;
	}
}

Resolver::UserAnswer::Type askUserAboutSolution(
		const Config& config, bool isDangerous, bool& addArgumentsFlag)
{
	string answer;

	if (config.getBool("cupt::console::assume-yes"))
	{
		answer = isDangerous ? "q" : "y";
		if (isDangerous)
		{
			cout << __("Didn't confirm dangerous actions.") << endl;
		}
	}
	else
	{
		ask:
		cout << __("Do you want to continue? [y/N/q/a/?] ");
		std::getline(std::cin, answer);
		if (!std::cin)
		{
			return Resolver::UserAnswer::Abandon;
		}
		FORIT(it, answer)
		{
			*it = std::tolower(*it); // lowercasing
		}
	}

	// deciding
	if (answer == "y")
	{
		if (isDangerous)
		{
			const string confirmationForDangerousAction = __("Yes, do as I say!");
			cout << format2(__("Dangerous actions selected. Type '%s' if you want to continue, or anything else to go back:"),
					confirmationForDangerousAction) << endl;
			std::getline(std::cin, answer);
			if (answer != confirmationForDangerousAction)
			{
				goto ask;
			}
		}
		return Resolver::UserAnswer::Accept;
	}
	else if (answer == "q")
	{
		return Resolver::UserAnswer::Abandon;
	}
	else if (answer == "a")
	{
		addArgumentsFlag = true;
		return Resolver::UserAnswer::Abandon;
	}
	else if (answer == "?")
	{
		cout << __("y: accept the solution") << endl;
		cout << __("n: reject the solution, try to find other ones") << endl;
		cout << __("q: reject the solution and exit") << endl;
		cout << __("a: specify an additional binary package expression") << endl;
		cout << __("?: output this help") << endl << endl;
		goto ask;
	}
	else
	{
		// user haven't chosen this solution, try next one
		cout << __("Resolving further... ") << endl;
		return Resolver::UserAnswer::Decline;
	}
}

Resolver::SuggestedPackages generateNotPolicyVersionList(const shared_ptr< const Cache >& cache,
		const Resolver::SuggestedPackages& packages)
{
	Resolver::SuggestedPackages result;
	FORIT(suggestedPackageIt, packages)
	{
		const shared_ptr< const BinaryVersion >& suggestedVersion = suggestedPackageIt->second.version;
		if (suggestedVersion)
		{
			auto policyVersion = cache->getPolicyVersion(getBinaryPackage(cache, suggestedVersion->packageName));
			if (!(*policyVersion == *suggestedVersion))
			{
				result.insert(*suggestedPackageIt);
			}
		}
	}

	return result;
}

bool wasOrWillBePackageAutoInstalled(const Cache& cache, const string& packageName,
		const map< string, bool >& autoFlagChanges)
{
	if (cache.isAutomaticallyInstalled(packageName))
	{
		return true;
	}

	auto autoFlagChangeIt = autoFlagChanges.find(packageName);
	if (autoFlagChangeIt != autoFlagChanges.end())
	{
		if (autoFlagChangeIt->second) // "set as autoinstalled"
		{
			return true;
		}
	}

	return false;
}

void addActionToSummary(const Cache& cache, const string& actionName,
		const Resolver::SuggestedPackages& suggestedPackages, const map< string, bool >& autoFlagChanges,
		std::stringstream* summaryStreamPtr)
{
	size_t manuallyInstalledCount = std::count_if(suggestedPackages.begin(), suggestedPackages.end(),
			[&cache, &autoFlagChanges](const pair< string, Resolver::SuggestedPackage >& arg)
			{
				return !wasOrWillBePackageAutoInstalled(cache, arg.first, autoFlagChanges);
			});

	auto total = suggestedPackages.size();
	*summaryStreamPtr << format2(__("  %u manually installed and %u automatically installed packages %s"),
			manuallyInstalledCount, total - manuallyInstalledCount, actionName) << endl;
}

Resolver::CallbackType generateManagementPrompt(const shared_ptr< const Config >& config,
		const shared_ptr< const Cache >& cache, const shared_ptr< Worker >& worker,
		bool showVersions, bool showSizeChanges, bool showNotPreferred,
		const set< string >& purgedPackageNames, bool& addArgumentsFlag, bool& thereIsNothingToDo)
{
	auto result = [&config, &cache, &worker, showVersions, showSizeChanges, showNotPreferred,
			&purgedPackageNames, &addArgumentsFlag, &thereIsNothingToDo]
			(const Resolver::Offer& offer) -> Resolver::UserAnswer::Type
	{
		addArgumentsFlag = false;
		thereIsNothingToDo = false;

		auto showReasons = config->getBool("cupt::resolver::track-reasons");
		auto summaryOnly = config->getBool("cupt::console::actions-preview::show-only-summary");

		worker->setDesiredState(offer);
		FORIT(packageNameIt, purgedPackageNames)
		{
			worker->setPackagePurgeFlag(*packageNameIt, true);
		}

		auto actionsPreview = worker->getActionsPreview();
		auto unpackedSizesPreview = worker->getUnpackedSizesPreview();

		std::stringstream summaryStream;

		size_t actionCount = 0;
		{ // print planned actions
			const map< WA::Type, string > actionNames = {
				{ WA::Install, __("will be installed") },
				{ WA::Remove, __("will be removed") },
				{ WA::Upgrade, __("will be upgraded") },
				{ WA::Purge, __("will be purged") },
				{ WA::Downgrade, __("will be downgraded") },
				{ WA::Configure, __("will be configured") },
				{ WA::Deconfigure, __("will be deconfigured") },
				{ WA::ProcessTriggers, __("will have triggers processed") },
				{ fakeNotPolicyVersionAction, __("will have a not preferred version") },
			};
			cout << endl;

			vector< WA::Type > actionTypesInOrder = { WA::Install, WA::Upgrade, WA::Remove,
					WA::Purge, WA::Downgrade, WA::Configure, WA::ProcessTriggers, WA::Deconfigure };
			if (showNotPreferred)
			{
				actionTypesInOrder.push_back(fakeNotPolicyVersionAction);
			}

			FORIT (actionTypeIt, actionTypesInOrder)
			{
				const WA::Type& actionType = *actionTypeIt;
				const Resolver::SuggestedPackages& actionSuggestedPackages =
						actionType == fakeNotPolicyVersionAction ?
						generateNotPolicyVersionList(cache, offer.suggestedPackages) : actionsPreview->groups[actionType];
				if (actionSuggestedPackages.empty())
				{
					continue;
				}

				if (actionType != fakeNotPolicyVersionAction)
				{
					actionCount += actionSuggestedPackages.size();
				}

				const string& actionName = actionNames.find(actionType)->second;

				addActionToSummary(*cache, actionName, actionSuggestedPackages,
						actionsPreview->autoFlagChanges, &summaryStream);
				if (summaryOnly)
				{
					continue;
				}

				cout << format2(__("The following packages %s:"), actionName) << endl << endl;

				FORIT(it, actionSuggestedPackages)
				{
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
						showVersion(cache, packageName, it->second, actionType);
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
			thereIsNothingToDo = true;
			return Resolver::UserAnswer::Abandon;
		}

		showUnsatisfiedSoftDependencies(offer, &summaryStream);

		{ // show summary
			cout << __("Action summary:") << endl << summaryStream.str() << endl;
			summaryStream.clear();
		}

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
		if (!actionsPreview->groups[WA::Downgrade].empty())
		{
			isDangerousAction = true;
		}

		return askUserAboutSolution(*config, isDangerousAction, addArgumentsFlag);
	};

	return result;
}

void parseManagementOptions(Context& context, ManagePackages::Mode mode,
		vector< string >& packageExpressions,
		bool& showVersions, bool& showSizeChanges, bool& showNotPreferred)
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
		("show-not-preferred", "")
		("download-only,d", "")
		("summary-only", "")
		("assume-yes", "")
		("yes,y", "");

	// use action modifiers as arguments, not options
	auto extraParser = [](const string& input) -> pair< string, string >
	{
		const set< string > actionModifierOptionNames = {
			"--install", "--remove", "--purge", "--satisfy", "--unsatisfy"
		};
		if (actionModifierOptionNames.count(input))
		{
			return make_pair("arguments", input);
		}
		else
		{
			return make_pair(string(), string());
		}
	};
	auto variables = parseOptions(context, options, packageExpressions, extraParser);

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
	if (variables.count("summary-only"))
	{
		config->setScalar("cupt::console::actions-preview::show-only-summary", "yes");
	}

	showVersions = variables.count("show-versions");
	showSizeChanges = variables.count("show-size-changes");
	string showNotPreferredConfigValue = config->getString("cupt::console::actions-preview::show-not-preferred");
	showNotPreferred = variables.count("show-not-preferred") ||
			showNotPreferredConfigValue == "yes" ||
			((mode == ManagePackages::FullUpgrade || mode == ManagePackages::SafeUpgrade) &&
					showNotPreferredConfigValue == "for-upgrades");
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
	bool showVersions, showSizeChanges, showNotPreferred;
	parseManagementOptions(context, mode, packageExpressions,
			showVersions, showSizeChanges, showNotPreferred);

	unrollFileArguments(packageExpressions);

	// snapshot handling
	string snapshotName; // empty if not applicable
	if (mode == ManagePackages::LoadSnapshot)
	{
		if (packageExpressions.size() != 1)
		{
			fatal2("exactly one argument (the snapshot name) should be specified");
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
				config->getString("cupt::resolver::synchronize-by-source-versions") != "none");

		cout << __("Building the package cache... ") << endl;
		cache = context.getCache(buildSource, true, true, packageNameGlobsToReinstall);
	}

	cout << __("Initializing package resolver and worker... ") << endl;
	std::unique_ptr< Resolver > resolver;
	{
		if (!config->getString("cupt::resolver::external-command").empty())
		{
			fatal2("using external resolver is not supported now");
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

	set< string > purgedPackageNames;
	processPackageExpressions(config, cache, mode, *resolver, packageExpressions, purgedPackageNames);

	cout << __("Resolving possible unmet dependencies... ") << endl;

	bool addArgumentsFlag, thereIsNothingToDo;
	auto callback = generateManagementPrompt(config, cache, worker,
			showVersions, showSizeChanges, showNotPreferred,
			purgedPackageNames, addArgumentsFlag, thereIsNothingToDo);

	resolve:
	bool resolved = resolver->resolve(callback);
	if (addArgumentsFlag && std::cin)
	{
		string answer;
		do
		{
			cout << __("Enter a package expression (empty to finish): ");
			std::getline(std::cin, answer);
			if (!answer.empty())
			{
				processPackageExpressions(config, cache, mode, *resolver,
						vector< string > { answer }, purgedPackageNames);
			}
			else
			{
				break;
			}
		} while (true);
		goto resolve;
	}

	// at this stage resolver has done its work, so to does not consume the RAM
	resolver.reset();

	if (thereIsNothingToDo)
	{
		cout << __("Nothing to do.") << endl;
		return 0;
	}
	else if (resolved)
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
			fatal2("unable to do requested actions");
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
		context.unparsed.push_back("cupt");
		if (managePackages(context, ManagePackages::Install) != 0)
		{
			fatal2("upgrading of the package management tools failed");
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
		fatal2e("upgrading the system failed: execvp failed");
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
	auto cache = context.getCache(false, false, true);

	Worker worker(config, cache);

	FORIT(packageNameIt, arguments)
	{
		getBinaryPackage(cache, *packageNameIt); // check that it exists
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
			fatal2e("lstat() failed: '%s'", path);
		}
		else
		{
			size_t size = stat_structure.st_size;
			totalDeletedBytes += size;
			cout << format2(__("Deleting '%s' <%s>..."), path, humanReadableSizeString(size)) << endl;
			worker.deleteArchive(path);
		}
	}
	cout << format2(__("Freed %s of disk space."), humanReadableSizeString(totalDeletedBytes)) << endl;

	cout << __("Deleting partial archives...") << endl;
	worker.deletePartialArchives();

	return 0;
}

