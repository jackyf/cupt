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
#include <stack>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <boost/lexical_cast.hpp>

#include "../common.hpp"
#include "../handlers.hpp"
#include "../misc.hpp"
#include "../selectors.hpp"
#include "../colorizer.hpp"

#include <cupt/system/state.hpp>
#include <cupt/system/resolver.hpp>
#include <cupt/system/resolvers/native.hpp>
#include <cupt/system/snapshots.hpp>
#include <cupt/file.hpp>
#include <cupt/system/worker.hpp>
#include <cupt/cache/sourceversion.hpp>
#include <cupt/cache/releaseinfo.hpp>
#include <cupt/packagename.hpp>
#include <cupt/versionstring.hpp>

typedef Worker::Action WA;
const WA::Type fakeNotPreferredVersionAction = WA::Type(999);
const WA::Type fakeAutoRemove = WA::Type(1000);
const WA::Type fakeAutoPurge = WA::Type(1001);
const WA::Type fakeBecomeAutomaticallyInstalled = WA::Type(1002);
const WA::Type fakeBecomeManuallyInstalled = WA::Type(1003);

struct VersionChoices
{
	string packageName;
	vector< const BinaryVersion* > versions;

	string getFullAnnotation(const string& requestAnnotation) const
	{
		return requestAnnotation + format2(" | for package '%s'", packageName);
	}
};

struct ManagePackagesContext
{
	enum class AutoInstall { Yes, No, Nop };
	enum class SelectType { Traditional, Flexible };

	ManagePackages::Mode mode;
	AutoInstall autoinstall;
	SelectType selectType;
	Resolver::RequestImportance importance;
	Config* config;
	const Cache* cache;
	Resolver* resolver;
	Worker* worker;
	bool oneLetterSuffixDeprecationWarningIssued = false;

	typedef vector< VersionChoices > SelectedVersions;

	SelectedVersions selectVersions(const string& expression, bool throwOnError = true)
	{
		auto selector = (selectType == SelectType::Traditional ?
				selectBinaryVersionsWildcarded : selectAllBinaryVersionsWildcarded);

		SelectedVersions result;
		// grouping by package
		for (auto version: selector(*cache, expression, throwOnError))
		{
			if (result.empty() || result.back().packageName != version->packageName)
			{
				result.push_back(VersionChoices{version->packageName, {}});
			}
			result.back().versions.push_back(version);
		}
		return result;
	}
	void install(const VersionChoices& versionChoices, const string& requestAnnotation)
	{
		resolver->installVersion(versionChoices.versions,
				versionChoices.getFullAnnotation(requestAnnotation), importance);
	}
	void remove(const VersionChoices& versionChoices, const string& requestAnnotation)
	{
		const auto& fullAnnotation = versionChoices.getFullAnnotation(requestAnnotation);

		if (selectType == SelectType::Traditional)
		{
			// removing all the package regardless of what versions of packages
			// were chosen
			if (!versionChoices.versions.empty())
			{
				const string& packageName = versionChoices.packageName;
				resolver->removeVersions(cache->getBinaryPackage(packageName)->getVersions(), fullAnnotation, importance);
			}
		}
		else
		{
			resolver->removeVersions(versionChoices.versions, fullAnnotation, importance);
		}
	}
	void satisfy(const RelationExpression& relationExpression, bool inverted, const string& requestAnnotation)
	{
		resolver->satisfyRelationExpression(relationExpression,
				inverted, requestAnnotation, importance, autoinstall != AutoInstall::No);
	}
};

static const char* modeToString(ManagePackages::Mode mode)
{
	switch (mode)
	{
		case ManagePackages::Install: return "install";
		case ManagePackages::InstallIfInstalled: return "iii";
		case ManagePackages::Remove: return "remove";
		case ManagePackages::Purge: return "purge";
		case ManagePackages::BuildDepends: return "build-dependencies of";
		case ManagePackages::Reinstall: return "reinstall";
		default: return "";
	}
}

static string getRequestAnnotation(ManagePackages::Mode mode, const string& expression)
{
	return format2("%s %s", modeToString(mode), expression);
}

static void preProcessMode(ManagePackagesContext& mpc)
{
	if (mpc.mode == ManagePackages::SelfUpgrade) {
		mpc.mode = ManagePackages::Install;
	}
	if (mpc.mode == ManagePackages::FullUpgrade || mpc.mode == ManagePackages::SafeUpgrade)
	{
		if (mpc.mode == ManagePackages::SafeUpgrade)
		{
			mpc.config->setScalar("cupt::resolver::no-remove", "yes");
		}
		mpc.resolver->upgrade();

		// despite the main action is {safe,full}-upgrade, allow package
		// modifiers in the command line just as with the install command
		mpc.mode = ManagePackages::Install;
	}
	else if (mpc.mode == ManagePackages::Satisfy || mpc.mode == ManagePackages::BuildDepends)
	{
		mpc.config->setScalar("apt::install-recommends", "no");
		mpc.config->setScalar("apt::install-suggests", "no");
	}
	else if (mpc.mode == ManagePackages::BuildDepends)
	{
		mpc.satisfy(RelationExpression("build-essential"), false, string());
	}
}

static void unrollFileArguments(vector< string >& arguments)
{
	vector< string > newArguments;
	for (const string& argument: arguments)
	{
		if (!argument.empty() && argument[0] == '@')
		{
			const string path = argument.substr(1);
			// reading package expressions from file
			RequiredFile file(path, "r");
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

void __satisfy_or_unsatisfy(ManagePackagesContext& mpc,
		const RelationLine& relationLine, ManagePackages::Mode mode, const string& annotation = string())
{
	for (const auto& relationExpression: relationLine)
	{
		mpc.satisfy(relationExpression, (mode == ManagePackages::Unsatisfy), annotation);
	}
}

static void processSatisfyExpression(ManagePackagesContext& mpc, string packageExpression)
{
	auto localMode = mpc.mode;

	if (localMode == ManagePackages::Satisfy && !packageExpression.empty() && *(packageExpression.rbegin()) == '-')
	{
		localMode = ManagePackages::Unsatisfy;
		packageExpression.erase(packageExpression.end() - 1);
	}

	auto relationLine = ArchitecturedRelationLine(packageExpression)
			.toRelationLine(mpc.config->getString("apt::architecture"));

	__satisfy_or_unsatisfy(mpc, relationLine, localMode);
}

static void processBuildDependsExpression(ManagePackagesContext& mpc, const string& packageExpression)
{
	auto architecture = mpc.config->getString("apt::architecture");

	auto versions = selectSourceVersionsWildcarded(*mpc.cache, packageExpression);
	auto annotation = getRequestAnnotation(mpc.mode, packageExpression);

	for (const auto& version: versions)
	{
		__satisfy_or_unsatisfy(mpc, version->relations[SourceVersion::RelationTypes::BuildDepends]
				.toRelationLine(architecture), ManagePackages::Satisfy, annotation);
		__satisfy_or_unsatisfy(mpc, version->relations[SourceVersion::RelationTypes::BuildDependsIndep]
				.toRelationLine(architecture), ManagePackages::Satisfy, annotation);
		__satisfy_or_unsatisfy(mpc, version->relations[SourceVersion::RelationTypes::BuildDependsArch]
				.toRelationLine(architecture), ManagePackages::Satisfy, annotation);
		__satisfy_or_unsatisfy(mpc, version->relations[SourceVersion::RelationTypes::BuildConflicts]
				.toRelationLine(architecture), ManagePackages::Unsatisfy, annotation);
		__satisfy_or_unsatisfy(mpc, version->relations[SourceVersion::RelationTypes::BuildConflictsIndep]
				.toRelationLine(architecture), ManagePackages::Unsatisfy, annotation);
		__satisfy_or_unsatisfy(mpc, version->relations[SourceVersion::RelationTypes::BuildConflictsArch]
				.toRelationLine(architecture), ManagePackages::Unsatisfy, annotation);
	}
}

static void processInstallOrRemoveExpression(ManagePackagesContext& mpc, string packageExpression)
{
	auto localMode = mpc.mode;

	auto versions = mpc.selectVersions(packageExpression, false);
	if (versions.empty())
	{
		/* we have a funny situation with package names like 'g++',
		   where one don't know is there simple package name or '+'/'-'
		   modifier at the end of package name, so we enter here only if
		   it seems that there is no such binary package */

		// "localizing" action to make it modifiable by package modifiers
		if (!packageExpression.empty())
		{
			const auto oldPackageExpression = packageExpression;

			const char& lastLetter = *(packageExpression.end() - 1);
			if (lastLetter == '+')
			{
				localMode = ManagePackages::Install;
				packageExpression.erase(packageExpression.end() - 1);
			}
			else if (lastLetter == '-')
			{
				localMode = ManagePackages::Remove;
				packageExpression.erase(packageExpression.end() - 1);
			}

			if (!mpc.oneLetterSuffixDeprecationWarningIssued && oldPackageExpression != packageExpression)
			{
				warn2("Package suffixes '+' and '-' are deprecated. Please use '--install' and '--remove', respectively.");
				mpc.oneLetterSuffixDeprecationWarningIssued = true;
			}
		}
	}

	if (localMode == ManagePackages::Install || localMode == ManagePackages::InstallIfInstalled)
	{
		if (versions.empty())
		{
			versions = mpc.selectVersions(packageExpression);
		}
		for (const auto& versionChoices: versions)
		{
			const auto& packageName = versionChoices.packageName;

			if (localMode == ManagePackages::InstallIfInstalled)
			{
				if (!isPackageInstalled(*mpc.cache, packageName))
				{
					continue;
				}
			}
			mpc.install(versionChoices, getRequestAnnotation(localMode, packageExpression));

			if (mpc.autoinstall == ManagePackagesContext::AutoInstall::Yes)
			{
				mpc.resolver->setAutomaticallyInstalledFlag(packageName, true);
			}
			else if (mpc.autoinstall == ManagePackagesContext::AutoInstall::No)
			{
				mpc.resolver->setAutomaticallyInstalledFlag(packageName, false);
			}
		}
	}
	else // ManagePackages::Remove or ManagePackages::Purge
	{
		if (versions.empty())
		{
			// retry, still non-fatal to deal with packages in 'config-files' state
			versions = mpc.selectVersions(packageExpression, false);
		}

		auto scheduleRemoval = [&mpc, localMode, &packageExpression](const VersionChoices& versionChoices)
		{
			mpc.remove(versionChoices, getRequestAnnotation(localMode, packageExpression));
			if (localMode == ManagePackages::Purge)
			{
				mpc.worker->setPackagePurgeFlag(versionChoices.packageName, true);
			}
		};

		if (!versions.empty())
		{
			for (const auto& versionChoices: versions)
			{
				scheduleRemoval(versionChoices);
			}
		}
		else
		{
			checkPackageName(packageExpression);
			if (!mpc.cache->getSystemState()->getInstalledInfo(packageExpression) &&
				!getBinaryPackage(*mpc.cache, packageExpression, false))
			{
				fatal2(__("unable to find binary package/expression '%s'"), packageExpression);
			}

			scheduleRemoval(VersionChoices{packageExpression, {}});
		}
	}
}

static void processAutoFlagChangeExpression(ManagePackagesContext& mpc,
		const string& packageExpression)
{
	getBinaryPackage(*mpc.cache, packageExpression); // will throw if package name is wrong

	mpc.resolver->setAutomaticallyInstalledFlag(packageExpression,
			(mpc.mode == ManagePackages::Markauto));
}

static void processReinstallExpression(ManagePackagesContext& mpc, const string& packageExpression)
{
	auto package = getBinaryPackage(*mpc.cache, packageExpression);
	auto installedVersion = package->getInstalledVersion();
	if (!installedVersion)
	{
		fatal2(__("the package '%s' is not installed"), packageExpression);
	}
	const auto targetVersionString = getOriginalVersionString(installedVersion->versionString);

	std::vector<const BinaryVersion*> candidates;
	for (auto version: *package)
	{
		if (version != installedVersion)
		{
			if (getOriginalVersionString(version->versionString).equal(targetVersionString))
			{
				candidates.push_back(version);
			}
		}
	}

	if (!candidates.empty())
	{
		mpc.resolver->installVersion(candidates, getRequestAnnotation(mpc.mode, packageExpression), mpc.importance);
	}
	else
	{
		const auto message = format2(__("the package '%s' cannot be reinstalled because there is no corresponding version (%s) available in repositories"),
				packageExpression, targetVersionString.toStdString());
		if (mpc.importance == Resolver::RequestImportance::Must)
		{
			fatal2(message);
		}
		else
		{
			warn2(message);
		}
	}
}

static bool processNumericImportanceOption(ManagePackagesContext& mpc, const string& arg)
{
	const char field[] = "--importance=";
	const size_t fieldLength = sizeof(field)-1;
	if (arg.compare(0, fieldLength, field) == 0)
	{
		try
		{
			mpc.importance = boost::lexical_cast< Resolver::RequestImportance::Value >(arg.substr(fieldLength));
		}
		catch (boost::bad_lexical_cast&)
		{
			fatal2("option '--importance' requires non-negative numeric value");
		}
		return true;
	}
	else
	{
		return false;
	}
}

static bool processPositionalOption(ManagePackagesContext& mpc, const string& arg)
{
	if (arg == "--remove") mpc.mode = ManagePackages::Remove;
	else if (arg == "--purge") mpc.mode = ManagePackages::Purge;
	else if (arg == "--install") mpc.mode = ManagePackages::Install;
	else if (arg == "--satisfy") mpc.mode = ManagePackages::Satisfy;
	else if (arg == "--unsatisfy") mpc.mode = ManagePackages::Unsatisfy;
	else if (arg == "--markauto") mpc.mode = ManagePackages::Markauto;
	else if (arg == "--unmarkauto") mpc.mode = ManagePackages::Unmarkauto;
	else if (arg == "--iii") mpc.mode = ManagePackages::InstallIfInstalled;
	else if (arg == "--reinstall") mpc.mode = ManagePackages::Reinstall;
	else if (arg == "--asauto=yes") mpc.autoinstall = ManagePackagesContext::AutoInstall::Yes;
	else if (arg == "--asauto=no") mpc.autoinstall = ManagePackagesContext::AutoInstall::No;
	else if (arg == "--asauto=default") mpc.autoinstall = ManagePackagesContext::AutoInstall::Nop;
	else if (arg == "--select=traditional" || arg == "--st")
		mpc.selectType = ManagePackagesContext::SelectType::Traditional;
	else if (arg == "--select=flexible" || arg == "--sf")
		mpc.selectType = ManagePackagesContext::SelectType::Flexible;
	else if (arg == "--importance=wish" || arg == "--wish") mpc.importance = Resolver::RequestImportance::Wish;
	else if (arg == "--importance=try" || arg == "--try") mpc.importance = Resolver::RequestImportance::Try;
	else if (arg == "--importance=must" || arg == "--must") mpc.importance = Resolver::RequestImportance::Must;
	else if (processNumericImportanceOption(mpc, arg)) ;
	else
		return false;

	return true; // if some option was processed
}

static void processPackageExpressions(ManagePackagesContext& mpc, const vector< string >& packageExpressions)
{
	for (const auto& packageExpression: packageExpressions)
	{
		if (processPositionalOption(mpc, packageExpression)) continue;

		if (mpc.mode == ManagePackages::Satisfy || mpc.mode == ManagePackages::Unsatisfy)
		{
			processSatisfyExpression(mpc, packageExpression);
		}
		else if (mpc.mode == ManagePackages::BuildDepends)
		{
			processBuildDependsExpression(mpc, packageExpression);
		}
		else if (mpc.mode == ManagePackages::Markauto || mpc.mode == ManagePackages::Unmarkauto)
		{
			processAutoFlagChangeExpression(mpc, packageExpression);
		}
		else if (mpc.mode == ManagePackages::Reinstall)
		{
			processReinstallExpression(mpc, packageExpression);
		}
		else
		{
			processInstallOrRemoveExpression(mpc, packageExpression);
		}
	}
}

void printUnpackedSizeChanges(const map< string, ssize_t >& unpackedSizesPreview)
{
	ssize_t totalUnpackedSizeChange = 0;
	for (const auto& previewRecord: unpackedSizesPreview)
	{
		totalUnpackedSizeChange += previewRecord.second;
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

struct VersionInfoFlags
{
	bool versionString;
	enum class DistributionType { None, Archive, Codename };
	DistributionType distributionType;
	bool component;
	bool vendor;
	bool showEmpty;

	VersionInfoFlags(const Config& config)
	{
		versionString = config.getBool("cupt::console::actions-preview::show-versions");
		if (config.getBool("cupt::console::actions-preview::show-archives"))
		{
			distributionType = DistributionType::Archive;
		}
		else if (config.getBool("cupt::console::actions-preview::show-codenames"))
		{
			distributionType = DistributionType::Codename;
		}
		else
		{
			distributionType = DistributionType::None;
		}
		component = config.getBool("cupt::console::actions-preview::show-components");
		vendor = config.getBool("cupt::console::actions-preview::show-vendors");
		showEmpty = config.getBool("cupt::console::actions-preview::show-empty-versions");
	}
	bool empty() const
	{
		return !versionString && distributionType == DistributionType::None && !component && !vendor;
	}
};
void showVersionInfoIfNeeded(const Cache& cache, const string& packageName,
		const Resolver::SuggestedPackage& suggestedPackage, WA::Type actionType,
		VersionInfoFlags flags)
{
	if (flags.empty())
	{
		return; // nothing to print
	}

	auto getVersionString = [&flags](const Version* version) -> string
	{
		if (!version)
		{
			return flags.showEmpty ? "<empty>" : "";
		}
		string result;
		if (flags.versionString)
		{
			result += version->versionString;
		}
		if ((flags.distributionType != VersionInfoFlags::DistributionType::None)
				|| flags.component || flags.vendor)
		{
			result += '(';
			vector< string > chunks;
			for (const auto& source: version->sources)
			{
				string chunk;
				if (flags.vendor && !source.release->vendor.empty())
				{
					chunk += source.release->vendor;
					chunk += ':';
				}
				if (flags.distributionType == VersionInfoFlags::DistributionType::Archive)
				{
					chunk += source.release->archive;
				}
				else if (flags.distributionType == VersionInfoFlags::DistributionType::Codename)
				{
					chunk += source.release->codename;
				}
				if (flags.component && !source.release->component.empty())
				{
					chunk += "/";
					chunk += source.release->component;
				}
				if (!chunk.empty() && std::find(chunks.begin(), chunks.end(), chunk) == chunks.end())
				{
					chunks.push_back(chunk);
				}
			}
			result += join(",", chunks);
			result += ')';
		}
		return result;
	};

	auto package = cache.getBinaryPackage(packageName);
	string oldVersionString = getVersionString(package ? package->getInstalledVersion() : nullptr);
	string newVersionString = getVersionString(suggestedPackage.version);

	if (!oldVersionString.empty() && !newVersionString.empty() &&
			(actionType != fakeNotPreferredVersionAction || oldVersionString != newVersionString))
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

	if (actionType == fakeNotPreferredVersionAction)
	{
		cout << ", " << __("preferred") << ": " << getVersionString(cache.getPreferredVersion(package));
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

void printByLine(const vector< string >& strings)
{
	cout << endl;
	for (const auto& s: strings)
	{
		cout << s << endl;
	}
	cout << endl;
}

void checkForUntrustedPackages(const Worker::ActionsPreview& actionsPreview,
		bool* isDangerous)
{
	vector< string > untrustedPackageNames;
	// generate loud warning for unsigned versions
	const WA::Type affectedActionTypes[] = {
		WA::Reinstall, WA::Install, WA::Upgrade, WA::Downgrade
	};

	for (auto actionType: affectedActionTypes)
	{
		for (const auto& suggestedRecord: actionsPreview.groups[actionType])
		{
			if (!(suggestedRecord.second.version->isVerified()))
			{
				untrustedPackageNames.push_back(suggestedRecord.first);
			}
		}
	}

	if (!untrustedPackageNames.empty())
	{
		*isDangerous = true;
		cout << __("WARNING! The untrusted versions of the following packages will be used:") << endl;
		printByLine(untrustedPackageNames);
	}
}

void checkForRemovalOfPackages(const Cache& cache,
		const Worker::ActionsPreview& actionsPreview, bool* isDangerous,
		const char* desc, const std::function<bool(const BinaryVersion*)>& predicate)
{
	vector<string> affectedPackageNames;

	const WA::Type affectedActionTypes[] = { WA::Remove, WA::Purge };
	for (auto actionType: affectedActionTypes)
	{
		for (const auto& suggestedRecord: actionsPreview.groups[actionType])
		{
			const string& packageName = suggestedRecord.first;
			auto package = cache.getBinaryPackage(packageName);
			if (package)
			{
				auto version = package->getInstalledVersion();
				if (version) // may return false when purge of config-files package when candidates available
				{
					if (predicate(version))
					{
						affectedPackageNames.push_back(packageName);
					}
				}
			}
		}
	}

	if (!affectedPackageNames.empty())
	{
		*isDangerous = true;
		cout << __(desc) << endl;
		printByLine(affectedPackageNames);
	}
}

void checkForRemovalOfEssentialPackages(const Cache& cache,
		const Worker::ActionsPreview& actionsPreview, bool* isDangerous)
{
	const char desc[] = "WARNING! The following essential packages will be removed:";
	checkForRemovalOfPackages(cache, actionsPreview, isDangerous, desc,
							  [](const BinaryVersion* v) { return v->essential; });
}

void checkForRemovalOfImportantPackages(const Cache& cache,
		const Worker::ActionsPreview& actionsPreview, bool* isDangerous)
{
	const char desc[] = "WARNING! The following important packages will be removed:";
	checkForRemovalOfPackages(cache, actionsPreview, isDangerous, desc,
							  [](const BinaryVersion* v) { return v->important; });
}

void checkForIgnoredHolds(const Cache& cache,
		const Worker::ActionsPreview& actionsPreview, bool* isDangerous)
{
	vector< string > ignoredHoldsPackageNames;

	const WA::Type affectedActionTypes[] = { WA::Install, WA::Upgrade,
			WA::Downgrade, WA::Remove, WA::Purge };
	for (auto actionType: affectedActionTypes)
	{
		for (const auto& suggestedRecord: actionsPreview.groups[actionType])
		{
			const string& packageName = suggestedRecord.first;
			auto installedInfo = cache.getSystemState()->getInstalledInfo(packageName);
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
		*isDangerous = true;
		cout << __("WARNING! The following packages on hold will change their state:") << endl;
		printByLine(ignoredHoldsPackageNames);
	}
}

void checkForMultiArchSystem(const Config& config, bool* isDangerousAction)
{
	auto foreignArchitectures = config.getList("cupt::cache::foreign-architectures");
	if (!foreignArchitectures.empty())
	{
		*isDangerousAction = true;
		cout << __("WARNING! You are running cupt on MultiArch-enabled system. This setup is not supported at the moment.") << endl;
		cout << __("Any actions may break your system.") << endl;
		cout << format2(__("Detected foreign architectures: %s"), join(", ", foreignArchitectures)) << endl;
		cout << endl;
	}
}

void checkAndPrintDangerousActions(const Config& config, const Cache& cache,
		const Worker::ActionsPreview& actionsPreview, bool* isDangerousAction)
{
	if (!config.getBool("cupt::console::allow-untrusted"))
	{
		checkForUntrustedPackages(actionsPreview, isDangerousAction);
	}
	if (config.getBool("cupt::console::warnings::removal-of-essential"))
	{
		checkForRemovalOfEssentialPackages(cache, actionsPreview, isDangerousAction);
	}
	if (config.getBool("cupt::console::warnings::removal-of-important"))
	{
		checkForRemovalOfImportantPackages(cache, actionsPreview, isDangerousAction);
	}
	checkForIgnoredHolds(cache, actionsPreview, isDangerousAction);
	checkForMultiArchSystem(config, isDangerousAction);

	if (!actionsPreview.groups[WA::Downgrade].empty())
	{
		*isDangerousAction = true;
	}
}

void showReason(const Resolver::SuggestedPackage& suggestedPackage)
{
	for (const auto& reason: suggestedPackage.reasons)
	{
		cout << "  " << __("reason: ") << reason->toString() << endl;
	}
	if (!suggestedPackage.reasonPackageNames.empty())
	{
		cout << "  " << __("caused by changes in: ") << join(", ", suggestedPackage.reasonPackageNames) << endl;
	}
	cout << endl;
}

void showUnsatisfiedSoftDependencies(const Resolver::Offer& offer,
		bool showDetails, std::stringstream* summaryStreamPtr)
{
	vector< string > messages;
	for (const auto& unresolvedProblem: offer.unresolvedProblems)
	{
		messages.push_back(unresolvedProblem->toString());
	}

	if (!messages.empty())
	{
		if (showDetails)
		{
			cout << __("Leave the following dependencies unresolved:") << endl;
			printByLine(messages);
		}

		*summaryStreamPtr << format2(__("  %u dependency problems will stay unresolved"),
				offer.unresolvedProblems.size()) << endl;
	}
}

void showReasonChainForAskedPackage(const Resolver::SuggestedPackages& suggestedPackages, const Worker::ActionsPreview& actionsPreview)
{
	auto isPackageChangingItsState = [&actionsPreview](const string& packageName)
	{
		for (const auto& group: actionsPreview.groups)
		{
			if (group.count(packageName)) return true;
		}
		return false;
	};

	cout << __("Enter a binary package name to show reason chain for (empty to cancel): ");
	string answer;
	std::getline(std::cin, answer);
	if (answer.empty()) return;

	const auto& topPackageName = answer;
	if (!isPackageChangingItsState(topPackageName))
	{
		cout << format2(__("The package '%s' is not going to change its state."), topPackageName) << endl;
		return;
	}

	struct PackageAndLevel
	{
		string packageName;
		size_t level;
	};
	std::stack< PackageAndLevel > reasonStack({ PackageAndLevel{ topPackageName, 0 } });

	cout << endl;
	while (!reasonStack.empty())
	{
		const string& packageName = reasonStack.top().packageName;
		if (!isPackageChangingItsState(packageName))
		{
			reasonStack.pop();
			continue;
		}

		auto reasonIt = suggestedPackages.find(packageName);
		const auto& reasons = reasonIt->second.reasons;
		if (reasons.empty())
		{
			fatal2i("no reasons available for the package '%s'", packageName);
		}
		const auto& reasonPtr = reasons[0];

		size_t level = reasonStack.top().level;
		cout << format2("%s%s: %s", string(level*2, ' '), packageName, reasonPtr->toString()) << endl;

		reasonStack.pop();

		for (const string& reasonPackageName: reasonIt->second.reasonPackageNames)
		{
			reasonStack.push({ reasonPackageName, level + 1 });
		}
	}
	cout << endl;
}

Resolver::UserAnswer::Type askUserAboutSolution(const Config& config,
		const Resolver::SuggestedPackages& suggestedPackages,
		const Worker::ActionsPreview& actionsPreview,
		bool isDangerous, bool& addArgumentsFlag)
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
		cout << __("Do you want to continue? [y/N/q/a/rc/?] ");
		std::getline(std::cin, answer);
		if (!std::cin)
		{
			return Resolver::UserAnswer::Abandon;
		}
		for (char& c: answer) { c = std::tolower(c); } // lowercasing
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
	else if (answer == "rc")
	{
		showReasonChainForAskedPackage(suggestedPackages, actionsPreview);
		goto ask;
	}
	else if (answer == "?")
	{
		cout << __("y: accept the solution") << endl;
		cout << __("n: reject the solution, try to find other ones") << endl;
		cout << __("q: reject the solution and exit") << endl;
		cout << __("a: specify an additional binary package expression") << endl;
		cout << __("rc: show a reason chain for a package") << endl;
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

Resolver::SuggestedPackages generateNotPreferredVersionList(const Cache& cache,
		const Resolver::SuggestedPackages& packages)
{
	Resolver::SuggestedPackages result;
	for (const auto& suggestedPackage: packages)
	{
		const auto& suggestedVersion = suggestedPackage.second.version;
		if (suggestedVersion)
		{
			auto preferredVersion = cache.getPreferredVersion(
					getBinaryPackage(cache, suggestedVersion->packageName));
			if (!(preferredVersion == suggestedVersion))
			{
				result.insert(suggestedPackage);
			}
		}
	}

	return result;
}

static string colorizeByActionType(const Colorizer& colorizer,
		const string& input, WA::Type actionType, bool isAutoInstalled)
{
	Colorizer::Color color = Colorizer::Default;
	switch (actionType)
	{
		case WA::Install: color = Colorizer::Cyan; break;
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wswitch"
		case WA::Remove: case fakeAutoRemove: color = Colorizer::Yellow; break;
		case WA::Upgrade: color = Colorizer::Green; break;
		case WA::Purge: case fakeAutoPurge: color = Colorizer::Red; break;
		#pragma GCC diagnostic pop
		case WA::Downgrade: color = Colorizer::Magenta; break;
		case WA::Configure: color = Colorizer::Blue; break;
		default: ;
	}
	return colorizer.colorize(input, color, !isAutoInstalled /* bold */);
}

bool wasOrWillBePackageAutoInstalled(const Resolver::SuggestedPackage& suggestedPackage)
{
	return suggestedPackage.automaticallyInstalledFlag;
}

static void printPackageName(const Colorizer& colorizer, const string& packageName,
		WA::Type actionType, const Resolver::SuggestedPackage& suggestedPackage)
{
	bool isAutoInstalled = wasOrWillBePackageAutoInstalled(suggestedPackage);
	cout << colorizeByActionType(colorizer, packageName, actionType, isAutoInstalled);
}

static string colorizeActionName(const Colorizer& colorizer, const string& actionName, WA::Type actionType)
{
	if (actionType != WA::Install && actionType != WA::Upgrade &&
			actionType != WA::Configure && actionType != WA::ProcessTriggers &&
			actionType != fakeAutoRemove && actionType != fakeAutoPurge &&
			actionType != fakeBecomeAutomaticallyInstalled &&
			actionType != fakeBecomeManuallyInstalled)
	{
		return colorizer.makeBold(actionName);
	}
	else
	{
		return actionName;
	}
}

void addActionToSummary(WA::Type actionType, const string& actionName,
		const Resolver::SuggestedPackages& suggestedPackages,
		Colorizer& colorizer, std::stringstream* summaryStreamPtr)
{
	size_t manuallyInstalledCount = std::count_if(suggestedPackages.begin(), suggestedPackages.end(),
			[](const pair< string, Resolver::SuggestedPackage >& arg)
			{
				return !wasOrWillBePackageAutoInstalled(arg.second);
			});
	size_t autoInstalledCount = suggestedPackages.size() - manuallyInstalledCount;

	auto getManualCountString = [manuallyInstalledCount, actionType, &colorizer]()
	{
		auto s = std::to_string(manuallyInstalledCount);
		return colorizeByActionType(colorizer, s, actionType, false);
	};
	auto getAutoCountString = [autoInstalledCount, actionType, &colorizer]()
	{
		auto s = std::to_string(autoInstalledCount);
		return colorizeByActionType(colorizer, s, actionType, true);
	};

	*summaryStreamPtr << "  ";
	if (!manuallyInstalledCount)
	{
		*summaryStreamPtr << format2(__("%s automatically installed packages %s"),
				getAutoCountString(), actionName);
	}
	else if (!autoInstalledCount)
	{
		*summaryStreamPtr << format2(__("%s manually installed packages %s"),
				getManualCountString(), actionName);
	}
	else
	{
		*summaryStreamPtr << format2(__("%s manually installed and %s automatically installed packages %s"),
				getManualCountString(), getAutoCountString(), actionName);
	}
	*summaryStreamPtr << endl;
}

struct PackageIndicators
{
	bool manuallyInstalled = false;
	bool autoInstalled = false;

	PackageIndicators(const Config& config, const Colorizer& colorizer)
	{
		if (colorizer.enabled())
			return;
		const string optionPrefix("cupt::console::actions-preview::package-indicators");
		manuallyInstalled = config.getBool(optionPrefix + "::manually-installed");
		autoInstalled = config.getBool(optionPrefix + "::automatically-installed");
	}
};

struct PackageChangeInfoFlags
{
	bool sizeChange;
	bool reasons;
	VersionInfoFlags versionFlags;
	PackageIndicators packageIndicators;

	PackageChangeInfoFlags(const Config& config, WA::Type actionType, const Colorizer& colorizer)
		: versionFlags(config)
		, packageIndicators(config, colorizer)
	{
		sizeChange = (config.getBool("cupt::console::actions-preview::show-size-changes") &&
				actionType != fakeNotPreferredVersionAction);
		reasons = (config.getBool("cupt::console::actions-preview::show-reasons") &&
				actionType != fakeNotPreferredVersionAction);
	}
	bool packagesTakeSameLine() const
	{
		return versionFlags.empty() && !sizeChange && !reasons;
	}
};

static void printPackageIndicators(const Resolver::SuggestedPackage& suggestedPackage, PackageIndicators indicators)
{
	bool isAutoInstalled = wasOrWillBePackageAutoInstalled(suggestedPackage);
	if (indicators.autoInstalled && isAutoInstalled)
		cout << "{a}";
	if (indicators.manuallyInstalled && !isAutoInstalled)
		cout << "{m}";
}

void showPackageChanges(const Config& config, const Cache& cache, Colorizer& colorizer, WA::Type actionType,
		const Resolver::SuggestedPackages& actionSuggestedPackages,
		const map< string, ssize_t >& unpackedSizesPreview)
{
	PackageChangeInfoFlags showFlags(config, actionType, colorizer);

	for (const auto& it: actionSuggestedPackages)
	{
		const string& packageName = it.first;
		const auto& suggestedPackage = it.second;

		printPackageName(colorizer, packageName, actionType, suggestedPackage);
		printPackageIndicators(suggestedPackage, showFlags.packageIndicators);

		showVersionInfoIfNeeded(cache, packageName, suggestedPackage, actionType, showFlags.versionFlags);

		if (showFlags.sizeChange)
			showSizeChange(unpackedSizesPreview.find(packageName)->second);

		if (!showFlags.packagesTakeSameLine())
			cout << endl;
		else
			cout << ' ';

		if (showFlags.reasons)
			showReason(suggestedPackage);
	}
	if (showFlags.packagesTakeSameLine())
		cout << endl;
	cout << endl;
}

Resolver::SuggestedPackages filterNoLongerNeeded(const Resolver::SuggestedPackages& source, bool invert)
{
	Resolver::SuggestedPackages result;
	for (const auto& suggestedPackage: source)
	{
		bool isNoLongerNeeded = false;
		for (const auto& reasonPtr: suggestedPackage.second.reasons)
		{
			if (dynamic_pointer_cast< const Resolver::AutoRemovalReason >(reasonPtr))
			{
				isNoLongerNeeded = true;
				break;
			}
		}
		if (isNoLongerNeeded != invert)
		{
			result.insert(result.end(), suggestedPackage);
		}
	}
	return result;
}

Resolver::SuggestedPackages filterPureAutoStatusChanges(const Cache& cache,
		const Worker::ActionsPreview& actionsPreview, bool targetAutoStatus)
{
	static const shared_ptr< Resolver::UserReason > userReason;
	Resolver::SuggestedPackages result;

	for (const auto& autoStatusChange: actionsPreview.autoFlagChanges)
	{
		if (autoStatusChange.second == targetAutoStatus)
		{
			const string& packageName = autoStatusChange.first;
			for (size_t ai = 0; ai < WA::Count; ++ai)
			{
				if (targetAutoStatus && ai != WA::Install) continue;
				if (!targetAutoStatus && (ai != WA::Remove && ai != WA::Purge)) continue;

				if (actionsPreview.groups[ai].count(packageName))
				{
					goto next_package; // natural, non-pure change
				}
			}

			{
				Resolver::SuggestedPackage& entry = result[packageName];
				entry.version = cache.getBinaryPackage(packageName)->getInstalledVersion();
				entry.automaticallyInstalledFlag = !targetAutoStatus;
				entry.reasons.push_back(userReason);
			}

			next_package: ;
		}
	}

	return result;
}

Resolver::SuggestedPackages getSuggestedPackagesByAction(const Cache& cache,
		const Resolver::Offer& offer,
		const Worker::ActionsPreview& actionsPreview, WA::Type actionType)
{
	switch (actionType)
	{
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wswitch"
		case fakeNotPreferredVersionAction:
			return generateNotPreferredVersionList(cache, offer.suggestedPackages);
		case fakeAutoRemove:
			return filterNoLongerNeeded(actionsPreview.groups[WA::Remove], false);
		case fakeAutoPurge:
			return filterNoLongerNeeded(actionsPreview.groups[WA::Purge], false);
		case fakeBecomeAutomaticallyInstalled:
			return filterPureAutoStatusChanges(cache, actionsPreview, true);
		case fakeBecomeManuallyInstalled:
			return filterPureAutoStatusChanges(cache, actionsPreview, false);
		#pragma GCC diagnostic pop
		case WA::Remove:
			return filterNoLongerNeeded(actionsPreview.groups[WA::Remove], true);
		case WA::Purge:
			return filterNoLongerNeeded(actionsPreview.groups[WA::Purge], true);
		default:
			return actionsPreview.groups[actionType];
	}
}

map< WA::Type, string > getActionDescriptionMap()
{
	return map< WA::Type, string > {
		{ WA::Install, __("will be installed") },
		{ WA::Remove, __("will be removed") },
		{ WA::Upgrade, __("will be upgraded") },
		{ WA::Purge, __("will be purged") },
		{ WA::Downgrade, __("will be downgraded") },
		{ WA::Configure, __("will be configured") },
		{ WA::Deconfigure, __("will be deconfigured") },
		{ WA::ProcessTriggers, __("will have triggers processed") },
		{ WA::Reinstall, __("will be reinstalled") },
		{ fakeNotPreferredVersionAction, __("will have a not preferred version") },
		{ fakeAutoRemove, __("are no longer needed and thus will be auto-removed") },
		{ fakeAutoPurge, __("are no longer needed and thus will be auto-purged") },
		{ fakeBecomeAutomaticallyInstalled, __("will be marked as automatically installed") },
		{ fakeBecomeManuallyInstalled, __("will be marked as manually installed") },
	};
}

vector< WA::Type > getActionTypesInPrintOrder(bool showNotPreferred)
{
	vector< WA::Type > result = {
		fakeBecomeAutomaticallyInstalled, fakeBecomeManuallyInstalled,
		WA::Reinstall, WA::Install, WA::Upgrade, WA::Remove,
		WA::Purge, WA::Downgrade, WA::Configure, WA::ProcessTriggers, WA::Deconfigure
	};
	if (showNotPreferred)
	{
		result.push_back(fakeNotPreferredVersionAction);
	}
	result.push_back(fakeAutoRemove);
	result.push_back(fakeAutoPurge);

	return result;
}

bool isSummaryEnabled(const Config& config, size_t actionCount)
{
	auto configValue = config.getString("cupt::console::actions-preview::show-summary");
	return (configValue == "yes") ||
			(actionCount > 100 && configValue == "auto");
}

Resolver::CallbackType generateManagementPrompt(ManagePackagesContext& mpc,
		bool showNotPreferred, bool& addArgumentsFlag, bool& thereIsNothingToDo)
{
	auto result = [&mpc, showNotPreferred, &addArgumentsFlag, &thereIsNothingToDo]
			(const Resolver::Offer& offer) -> Resolver::UserAnswer::Type
	{
		addArgumentsFlag = false;
		thereIsNothingToDo = false;

		auto showDetails = mpc.config->getBool("cupt::console::actions-preview::show-details");

		mpc.worker->setDesiredState(offer);
		auto actionsPreview = mpc.worker->getActionsPreview();
		auto unpackedSizesPreview = mpc.worker->getUnpackedSizesPreview();

		Colorizer colorizer(*mpc.config);

		std::stringstream summaryStream;

		size_t actionCount = 0;
		{ // print planned actions
			const auto actionDescriptions = getActionDescriptionMap();
			cout << endl;

			for (const WA::Type& actionType: getActionTypesInPrintOrder(showNotPreferred))
			{
				auto actionSuggestedPackages = getSuggestedPackagesByAction(*mpc.cache,
						offer, *actionsPreview, actionType);
				if (actionSuggestedPackages.empty()) continue;

				if (actionType != fakeNotPreferredVersionAction)
				{
					actionCount += actionSuggestedPackages.size();
				}

				const string& actionName = actionDescriptions.find(actionType)->second;

				addActionToSummary(actionType, actionName, actionSuggestedPackages,
						colorizer, &summaryStream);
				if (!showDetails) continue;

				cout << format2(__("The following packages %s:"),
						colorizeActionName(colorizer, actionName, actionType)) << endl << endl;

				showPackageChanges(*mpc.config, *mpc.cache, colorizer, actionType,
						actionSuggestedPackages, unpackedSizesPreview);
			}

			showUnsatisfiedSoftDependencies(offer, showDetails, &summaryStream);
		};

		// nothing to do maybe?
		if (actionCount == 0)
		{
			thereIsNothingToDo = true;
			return Resolver::UserAnswer::Abandon;
		}

		if (isSummaryEnabled(*mpc.config, actionCount))
		{
			cout << __("Action summary:") << endl << summaryStream.str() << endl;
			summaryStream.clear();
		}

		bool isDangerousAction = false;
		checkAndPrintDangerousActions(*mpc.config, *mpc.cache, *actionsPreview, &isDangerousAction);

		{ // print size estimations
			auto downloadSizesPreview = mpc.worker->getDownloadSizesPreview();
			printDownloadSizes(downloadSizesPreview);
			printUnpackedSizeChanges(unpackedSizesPreview);
		}

		return askUserAboutSolution(*mpc.config, offer.suggestedPackages, *actionsPreview, isDangerousAction, addArgumentsFlag);
	};

	return result;
}

void parseManagementOptions(Context& context, ManagePackages::Mode mode,
		vector< string >& packageExpressions, bool& showNotPreferred)
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
		("show-archives,A", "")
		("show-codenames,N", "")
		("show-components,C", "")
		("show-deps", "")
		("show-not-preferred", "")
		("show-vendors,O", "")
		("download-only,d", "")
		("summary-only", "")
		("no-summary", "")
		("assume-yes", "")
		("yes,y", "");

	// use action modifiers as arguments, not options
	auto extraParser = [](const string& input) -> pair< string, string >
	{
		ManagePackagesContext dummyMpc = {
			ManagePackages::Mode::Install,
			ManagePackagesContext::AutoInstall::Nop,
			ManagePackagesContext::SelectType::Traditional,
			0, nullptr, nullptr, nullptr, nullptr
		};
		if (processPositionalOption(dummyMpc, input))
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
		config->setScalar("cupt::console::actions-preview::show-reasons", "yes");
	}
	// we now always need reason tracking
	config->setScalar("cupt::resolver::track-reasons", "yes");

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
		config->setScalar("cupt::console::actions-preview::show-summary", "yes");
		config->setScalar("cupt::console::actions-preview::show-details", "no");
	}
	if (variables.count("no-summary"))
	{
		config->setScalar("cupt::console::actions-preview::show-summary", "no");
		config->setScalar("cupt::console::actions-preview::show-details", "yes");
	}
	if (variables.count("show-versions"))
	{
		config->setScalar("cupt::console::actions-preview::show-versions", "yes");
	}
	if (variables.count("show-size-changes"))
	{
		config->setScalar("cupt::console::actions-preview::show-size-changes", "yes");
	}
	if (variables.count("show-archives"))
	{
		config->setScalar("cupt::console::actions-preview::show-archives", "yes");
	}
	if (variables.count("show-codenames"))
	{
		config->setScalar("cupt::console::actions-preview::show-codenames", "yes");
	}
	if (config->getBool("cupt::console::actions-preview::show-archives") &&
			config->getBool("cupt::console::actions-preview::show-codenames"))
	{
		fatal2(__("options 'cupt::console::actions-preview::show-archives' and 'cupt::console::actions-preview::show-codenames' cannot be used together"));
	}
	if (variables.count("show-components"))
	{
		config->setScalar("cupt::console::actions-preview::show-components", "yes");
	}
	if (variables.count("show-vendors"))
	{
		config->setScalar("cupt::console::actions-preview::show-vendors", "yes");
	}

	string showNotPreferredConfigValue = config->getString("cupt::console::actions-preview::show-not-preferred");
	showNotPreferred = variables.count("show-not-preferred") ||
			showNotPreferredConfigValue == "yes" ||
			((mode == ManagePackages::FullUpgrade || mode == ManagePackages::SafeUpgrade) &&
					showNotPreferredConfigValue == "for-upgrades");

	if (mode == ManagePackages::SelfUpgrade)
	{
		packageExpressions = {"dpkg", "cupt"};
	}
}

Resolver* getResolver(const shared_ptr< const Config >& config,
		const shared_ptr< const Cache >& cache)
{
	if (!config->getString("cupt::resolver::external-command").empty())
	{
		fatal2(__("using an external resolver is not supported now"));
	}

	return new NativeResolver(config, cache);
}

void queryAndProcessAdditionalPackageExpressions(ManagePackagesContext& mpc)
{
	string answer;
	do
	{
		cout << __("Enter package expression(s) (empty to finish): ");
		std::getline(std::cin, answer);
		if (!answer.empty())
		{
			processPackageExpressions(mpc, convertLineToShellArguments(answer));
		}
		else
		{
			break;
		}
	} while (true);
}

class ProgressStage
{
	bool p_print;
 public:
	ProgressStage(const Config& config)
		: p_print(config.getBool("cupt::console::show-progress-messages"))
	{}
	void operator()(const char* message)
	{
		if (p_print)
		{
			cout << message << endl;
		}
	}
};

int managePackages(Context& context, ManagePackages::Mode mode)
{
	auto config = context.getConfig();
	ProgressStage stage(*config);

	// turn off info parsing, we don't need it
	if (!shellMode)
	{
		Version::parseInfoOnly = false;
	}

	Cache::memoize = true;

	vector< string > packageExpressions;
	bool showNotPreferred;
	parseManagementOptions(context, mode, packageExpressions, showNotPreferred);

	unrollFileArguments(packageExpressions);

	// snapshot handling
	string snapshotName; // empty if not applicable
	if (mode == ManagePackages::LoadSnapshot)
	{
		if (packageExpressions.size() != 1)
		{
			fatal2(__("exactly one argument (the snapshot name) should be specified"));
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
		// source packages are needed for for synchronizing source versions
		bool buildSource = (mode == ManagePackages::BuildDepends ||
				config->getString("cupt::resolver::synchronize-by-source-versions") != "none");

		stage(__("Building the package cache... "));
		cache = context.getCache(buildSource, true, true);
	}

	stage(__("Initializing package resolver and worker... "));
	std::unique_ptr< Resolver > resolver(getResolver(config, cache));

	if (!snapshotName.empty())
	{
		Snapshots snapshots(config);
		snapshots.setupResolverForSnapshotOnly(snapshotName, *cache, *resolver);
	}

	shared_ptr< Worker > worker(new Worker(config, cache));

	ManagePackagesContext mpc = { mode,
			ManagePackagesContext::AutoInstall::Nop, ManagePackagesContext::SelectType::Traditional, Resolver::RequestImportance::Must,
			config.get(), cache.get(), resolver.get(), worker.get() };

	stage(__("Scheduling requested actions... "));
	preProcessMode(mpc);
	processPackageExpressions(mpc, packageExpressions);

	stage(__("Resolving possible unmet dependencies... "));

	bool addArgumentsFlag, thereIsNothingToDo;
	auto callback = generateManagementPrompt(mpc,
			showNotPreferred, addArgumentsFlag, thereIsNothingToDo);

	resolve:
	addArgumentsFlag = false;
	thereIsNothingToDo = false;
	bool resolved = resolver->resolve(callback);
	if (addArgumentsFlag && std::cin)
	{
		queryAndProcessAdditionalPackageExpressions(mpc);
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

		auto downloadProgress = getDownloadProgress(*config);
		cout << __("Performing requested actions:") << endl;
		context.invalidate();
		try
		{
			worker->changeSystem(downloadProgress);
		}
		catch (Exception&)
		{
			fatal2(__("unable to do requested actions"));
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
	if (shellMode)
	{
		fatal2(__("'dist-upgrade' command cannot be run in the shell mode"));
	}

	{ // 1st stage: upgrading of package management tools
		cout << __("[ upgrading package management tools ]") << endl;
		cout << endl;
		if (managePackages(context, ManagePackages::SelfUpgrade) != 0)
		{
			fatal2(__("upgrading of the package management tools failed"));
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
		fatal2e(__("%s() failed"), "execvp");
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

	auto downloadProgress = getDownloadProgress(*config);
	Worker worker(config, cache);

	context.invalidate();
	// may throw exception
	worker.updateReleaseAndIndexData(downloadProgress);

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
	for (const auto& infoRecord: info)
	{
		if (leaveAvailable && infoRecord.second /* version is not empty */)
		{
			continue; // skip this one
		}

		const string& path = infoRecord.first;

		struct stat stat_structure;
		if (lstat(path.c_str(), &stat_structure))
		{
			fatal2e(__("%s() failed: '%s'"), "lstat", path);
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

