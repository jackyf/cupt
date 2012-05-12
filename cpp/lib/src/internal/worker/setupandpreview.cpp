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
#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/releaseinfo.hpp>
#include <cupt/system/state.hpp>

#include <internal/filesystem.hpp>

#include <internal/worker/setupandpreview.hpp>

namespace cupt {
namespace internal {

void SetupAndPreviewWorker::__generate_action_preview(const string& packageName,
		const Resolver::SuggestedPackage& suggestedPackage, bool purgeFlag)
{
	Action::Type action = Action::Count; // invalid

	const auto& supposedVersion = suggestedPackage.version;
	auto installedInfo = _cache->getSystemState()->getInstalledInfo(packageName);

	if (supposedVersion)
	{
		// some package version is to be installed
		if (!installedInfo)
		{
			// no installed info for package
			action = Action::Install;
		}
		else
		{
			// there is some installed info about package

			// determine installed version
			auto package = _cache->getBinaryPackage(packageName);
			if (!package)
			{
				fatal2i("the binary package '%s' does not exist", packageName);
			}
			auto installedVersion = package->getInstalledVersion();

			if (!installedVersion)
			{
				action = Action::Install;
			}
			else
			{
				bool isImproperlyInstalled = installedInfo->isBroken();

				if (installedInfo->status == State::InstalledRecord::Status::Installed ||
						isImproperlyInstalled)
				{
					auto versionComparisonResult = compareVersionStrings(
							supposedVersion->versionString, installedVersion->versionString);

					if (versionComparisonResult > 0)
					{
						action = Action::Upgrade;
					}
					else if (versionComparisonResult < 0)
					{
						action = Action::Downgrade;
					}
					else if (isImproperlyInstalled)
					{
						action = Action::Upgrade; // TODO/ABI Break/: Action::Reinstall
					}
				}
				else
				{
					if (installedVersion->versionString == supposedVersion->versionString)
					{
						// the same version, but the package was in some interim state
						if (installedInfo->status == State::InstalledRecord::Status::TriggersPending)
						{
							action = Action::ProcessTriggers;
						}
						else if (installedInfo->status != State::InstalledRecord::Status::TriggersAwaited)
						{
							action = Action::Configure;
						}
					}
					else
					{
						// some interim state, but other version
						action = Action::Install;
					}
				}
			}
		}
	}
	else
	{
		// package is to be removed
		if (installedInfo)
		{
			switch (installedInfo->status)
			{
				case State::InstalledRecord::Status::Installed:
				{
					action = (purgeFlag ? Action::Purge : Action::Remove);
				}
					break;
				case State::InstalledRecord::Status::ConfigFiles:
				{
					if (purgeFlag)
					{
						action = Action::Purge;
					}
				}
					break;
				default:
				{
					// package was in some interim state
					action = Action::Deconfigure;
				}
			}
		}
	}

	if (action != Action::Count)
	{
		__actions_preview->groups[action][packageName] = suggestedPackage;

		if (action == Action::Remove ||
			(action == Action::Purge && installedInfo &&
			installedInfo->status == State::InstalledRecord::Status::Installed))
		{
			/* in case of removing a package we delete the 'automatically
			   installed' info regardless was this flag set or not so next
			   time when this package is installed it has 'clean' info */
			__actions_preview->autoFlagChanges[packageName] = false;
		}
		else if (action == Action::Install && !suggestedPackage.manuallySelected)
		{
			// set 'automatically installed' for new non-manually selected packages
			__actions_preview->autoFlagChanges[packageName] = true;
		}
	}
}

void SetupAndPreviewWorker::__generate_actions_preview()
{
	__actions_preview.reset(new ActionsPreview);

	if (!__desired_state)
	{
		fatal2(__("worker: the desired state is not given"));
	}

	const bool purge = _config->getBool("cupt::worker::purge");

	FORIT(desiredIt, *__desired_state)
	{
		const string& packageName = desiredIt->first;
		const Resolver::SuggestedPackage& suggestedPackage = desiredIt->second;

		__generate_action_preview(packageName, suggestedPackage, purge);
	}
}

void SetupAndPreviewWorker::setDesiredState(const Resolver::Offer& offer)
{
	__desired_state.reset(new Resolver::SuggestedPackages(offer.suggestedPackages));
	__generate_actions_preview();
}

void SetupAndPreviewWorker::setPackagePurgeFlag(const string& packageName, bool value)
{
	auto desiredIt = __desired_state->find(packageName);
	if (desiredIt == __desired_state->end())
	{
		fatal2(__("there is no package '%s' in the desired state"), packageName);
	}
	auto sourceActionType = value ? Action::Remove : Action::Purge;
	__actions_preview->groups[sourceActionType].erase(packageName);

	// and regenerate
	__generate_action_preview(packageName, desiredIt->second, value);
}

shared_ptr< const Worker::ActionsPreview > SetupAndPreviewWorker::getActionsPreview() const
{
	return __actions_preview;
}

map< string, ssize_t > SetupAndPreviewWorker::getUnpackedSizesPreview() const
{
	map< string, ssize_t > result;

	set< string > changedPackageNames;
	for (size_t i = 0; i < Action::Count; ++i)
	{
		const Resolver::SuggestedPackages& suggestedPackages = __actions_preview->groups[i];
		FORIT(it, suggestedPackages)
		{
			changedPackageNames.insert(it->first);
		}
	}

	FORIT(packageNameIt, changedPackageNames)
	{
		const string& packageName = *packageNameIt;

		// old part
		ssize_t oldInstalledSize = 0;
		auto oldPackage = _cache->getBinaryPackage(packageName);
		if (oldPackage)
		{
			const auto& oldVersion = oldPackage->getInstalledVersion();
			if (oldVersion)
			{
				oldInstalledSize = oldVersion->installedSize;
			}
		}

		// new part
		ssize_t newInstalledSize = 0;
		const auto& newVersion = __desired_state->find(packageName)->second.version;
		if (newVersion)
		{
			newInstalledSize = newVersion->installedSize;
		}

		result[packageName] = newInstalledSize - oldInstalledSize;
	}


	return result;
}

pair< size_t, size_t > SetupAndPreviewWorker::getDownloadSizesPreview() const
{
	auto archivesDirectory = _get_archives_directory();
	size_t totalBytes = 0;
	size_t needBytes = 0;

	static const Action::Type affectedActionTypes[] = {
			Action::Install, Action::Upgrade, Action::Downgrade };
	for (size_t i = 0; i < sizeof(affectedActionTypes) / sizeof(Action::Type); ++i)
	{
		const Resolver::SuggestedPackages& suggestedPackages =
			__actions_preview->groups[affectedActionTypes[i]];

		FORIT(it, suggestedPackages)
		{
			const auto& version = it->second.version;
			auto size = version->file.size;

			totalBytes += size;
			needBytes += size; // for start

			auto basename = _get_archive_basename(version);
			auto path = archivesDirectory + "/" + basename;
			if (fs::fileExists(path))
			{
				if (version->file.hashSums.verify(path))
				{
					// ok, no need to download the file
					needBytes -= size;
				}
			}
		}
	}

	return std::make_pair(totalBytes, needBytes);
}

}
}

