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
#include <cstdlib>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cupt/system/worker.hpp>
#include <cupt/system/state.hpp>
#include <cupt/config.hpp>
#include <cupt/file.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/releaseinfo.hpp>
#include <cupt/hashsums.hpp>
#include <cupt/download/manager.hpp>
#include <cupt/download/progress.hpp>

#include <internal/graph.hpp>
#include <internal/common.hpp>
#include <internal/lock.hpp>
#include <internal/filesystem.hpp>
#include <internal/debdeltahelper.hpp>

namespace cupt {
namespace internal {

using std::list;

static const string __partial_directory_suffix = "/partial";
static const string __dummy_version_string = "<dummy>";

using system::Worker;
using system::Resolver;
using system::State;

struct Direction
{
	enum Type { After, Before };
};
struct InnerAction
{
	enum Type { Unpack, Remove, Configure } type;
	shared_ptr< const BinaryVersion > version;
	bool fake;
	string dpkgFlags;

	bool operator<(const InnerAction& other) const
	{
		if (type < other.type)
		{
			return true;
		}
		else if (type > other.type)
		{
			return false;
		}
		else
		{
			return *version < *(other.version);
		}
	}
	bool operator==(const InnerAction& other) const
	{
		return type == other.type && *version == *(other.version);
	}

	string toString() const
	{
		const static string typeStrings[] = { "unpack", "remove", "configure" };
		string prefix = fake ? "(fake)" : "";
		string result = prefix + typeStrings[type] + " " + version->packageName +
				" " + version->versionString;

		return result;
	}
};
struct GraphAndAttributes
{
	Graph< InnerAction > graph;
	struct RelationInfoRecord
	{
		BinaryVersion::RelationTypes::Type dependencyType;
		RelationExpression relationExpression;
		bool reverse;
	};
	struct Attribute
	{
		bool multiplied;
		vector< RelationInfoRecord > relationInfo;

		Attribute() : multiplied(false) {};
	};
	map< InnerAction, map< InnerAction, Attribute > > attributes;
};
struct Changeset
{
	vector< vector< InnerAction > > actionGroups;
	vector< pair< download::Manager::DownloadEntity, string > > downloads;
};

class WorkerImpl
{
	typedef Worker::ActionsPreview ActionsPreview;
	typedef Worker::Action Action;

	shared_ptr< const Config > __config;
	shared_ptr< const Cache > __cache;
	shared_ptr< const Resolver::SuggestedPackages > __desired_state;
	shared_ptr< const ActionsPreview > __actions_preview;
	mode_t __umask;
	std::set< string > __auto_installed_package_names;
	Lock* __lock;

	static string __get_archive_basename(const shared_ptr< const BinaryVersion >&);
	void __generate_actions_preview();
	string __get_archives_directory() const;
	string __get_indexes_directory() const;

	// managing packages
	void __fill_actions(GraphAndAttributes&);
	void __fill_graph_dependencies(GraphAndAttributes&);
	bool __build_actions_graph(GraphAndAttributes&);
	map< string, pair< download::Manager::DownloadEntity, string > > __prepare_downloads();
	vector< Changeset > __get_changesets(GraphAndAttributes&,
			const map< string, pair< download::Manager::DownloadEntity, string > >&);
	void __run_external_command(const string&, const string& = "");
	void __run_dpkg_command(const string&, const string&, const string&);
	void __do_dpkg_pre_actions();
	void __do_dpkg_post_actions();
	string __generate_input_for_preinstall_v2_hooks(const vector< vector< InnerAction > >&);
	void __do_dpkg_pre_packages_actions(const vector< vector< InnerAction > >&);
	void __clean_downloads(const Changeset& changeset);
	void __synchronize_apt_compat_symlinks();
	void __import_auto_installed_package_names();
	void __fill_action_dependencies(const shared_ptr< const BinaryVersion >&,
			BinaryVersion::RelationTypes::Type, InnerAction::Type,
			Direction::Type, const InnerAction&, GraphAndAttributes&, bool);
	void __do_downloads(const vector< pair< download::Manager::DownloadEntity, string > >&,
			const shared_ptr< download::Progress >&);
	static void __check_graph_pre_depends(GraphAndAttributes& gaa);

	// updating release and index data
	bool __update_release_and_index_data(download::Manager&, const Cache::IndexEntry&);
	bool __update_release(download::Manager&, const Cache::IndexEntry&, bool& releaseFileChanged);
	ssize_t __get_uri_priority(const string& uri);
	bool __update_index(download::Manager&, const Cache::IndexEntry&,
			bool releaseFileChanged, bool& indexFileChanged);
	void __update_translations(download::Manager& downloadManager,
		const Cache::IndexEntry&, bool indexFileChanged);
 public:
	WorkerImpl(const shared_ptr< const Config >& config, const shared_ptr< const Cache >& cache);
	~WorkerImpl();

	void setDesiredState(const Resolver::SuggestedPackages& desiredState);

	shared_ptr< const ActionsPreview > getActionsPreview() const;
	map< string, ssize_t > getUnpackedSizesPreview() const;
	pair< size_t, size_t > getDownloadSizesPreview() const;

	void markAsAutomaticallyInstalled(const string& packageName, bool targetStatus);
	void changeSystem(const shared_ptr< download::Progress >&);

	void updateReleaseAndIndexData(const shared_ptr< download::Progress >&);

	vector< pair< string, shared_ptr< const BinaryVersion > > > getArchivesInfo() const;
	void deleteArchive(const string& path);
};

WorkerImpl::WorkerImpl(const shared_ptr< const Config >& config, const shared_ptr< const Cache >& cache)
	: __config(config), __cache(cache)
{
	__umask = umask(0022);

	string lockPath = __config->getString("dir") + __config->getString("cupt::directory::state") + "/lock";
	__lock = new Lock(__config, lockPath);

	__import_auto_installed_package_names();
	__synchronize_apt_compat_symlinks();
}

WorkerImpl::~WorkerImpl()
{
	delete __lock;
	umask(__umask);
}

void WorkerImpl::__synchronize_apt_compat_symlinks()
{
	if (__config->getBool("cupt::worker::simulate"))
	{
		return;
	}

	auto archivesDirectory = __get_archives_directory();
	auto debPaths = fs::glob(archivesDirectory + "/*.deb");
	FORIT(debPathIt, debPaths)
	{
		const string& debPath = *debPathIt;
		if (!fs::exists(debPath))
		{
			// a dangling symlink
			if (unlink(debPath.c_str()) == -1)
			{
				warn("unable to delete dangling APT compatibility symbolic link '%s': EEE", debPath.c_str());
			}
		}
		else
		{
			// this is a regular file
			auto pathBasename = fs::filename(debPath);

			auto correctedBasename = pathBasename;
			auto offset = correctedBasename.find("%3a");
			if (offset != string::npos)
			{
				correctedBasename.replace(offset, 3, ":");
				auto correctedPath = archivesDirectory + "/" + correctedBasename;

				if (!fs::exists(correctedPath))
				{
					if (symlink(pathBasename.c_str(), correctedPath.c_str()) == -1)
					{
						fatal("unable to create APT compatibility symbolic link '%s' -> '%s': EEE",
								correctedPath.c_str(), pathBasename.c_str());
					}
				}
			}
		}
	}
}

void WorkerImpl::__import_auto_installed_package_names()
{
	__auto_installed_package_names = __cache->getExtendedInfo().automaticallyInstalled;
}

string WorkerImpl::__get_archives_directory() const
{
	return __config->getString("dir") +
			__config->getString("dir::cache") + '/' +
			__config->getString("dir::cache::archives");
}

string WorkerImpl::__get_indexes_directory() const
{
	return __config->getString("dir") +
			__config->getString("dir::state") + '/' +
			__config->getString("dir::state::lists");
}

string WorkerImpl::__get_archive_basename(const shared_ptr< const BinaryVersion >& version)
{
	return version->packageName + '_' + version->versionString + '_' +
			version->architecture + ".deb";
}

void WorkerImpl::__generate_actions_preview()
{
	shared_ptr< ActionsPreview > result(new ActionsPreview(Action::Count));

	if (!__desired_state)
	{
		fatal("worker desired state is not given");
	}

	const bool purge = __config->getBool("cupt::worker::purge");

	FORIT(desiredIt, *__desired_state)
	{
		const string& packageName = desiredIt->first;
		const Resolver::SuggestedPackage& suggestedPackage = desiredIt->second;

		Action::Type action = Action::Count; // invalid

		const shared_ptr< const BinaryVersion >& supposedVersion = suggestedPackage.version;
		auto installedInfo = __cache->getSystemState()->getInstalledInfo(packageName);

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
				auto package = __cache->getBinaryPackage(packageName);
				if (!package)
				{
					fatal("internal error: the binary package '%s' does not exist", packageName.c_str());
				}
				auto installedVersion = package->getInstalledVersion();
				if (installedInfo->status != State::InstalledRecord::Status::ConfigFiles && !installedVersion)
				{
					fatal("internal error: there is no installed version for the binary package '%s'",
							packageName.c_str());
				}

				switch (installedInfo->status)
				{
					case State::InstalledRecord::Status::ConfigFiles:
					{
						// treat as the same as uninstalled
						action = Action::Install;
					}
						break;
					case State::InstalledRecord::Status::Installed:
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
					}
						break;
					default:
					{
						if (installedVersion->versionString == supposedVersion->versionString)
						{
							// the same version, but the package was in some interim state
							action = Action::Configure;
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
						action = (purge ? Action::Purge : Action::Remove);
					}
					case State::InstalledRecord::Status::ConfigFiles:
					{
						if (purge)
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
			(*result)[action][packageName] = suggestedPackage;

			if (action == Action::Remove ||
				(action == Action::Purge && installedInfo &&
				installedInfo->status == State::InstalledRecord::Status::Installed))
			{
				/* in case of removing a package we delete the 'automatically
				   installed' info regardless was this flag set or not so next
				   time when this package is installed it has 'clean' info */
				(*result)[Action::Unmarkauto][packageName] = suggestedPackage;
			}
			else if (action == Action::Install && !suggestedPackage.manuallySelected)
			{
				// set 'automatically installed' for new non-manually selected packages
				(*result)[Action::Markauto][packageName] = suggestedPackage;
			}
		}
	}

	__actions_preview = result;
}

void WorkerImpl::setDesiredState(const Resolver::SuggestedPackages& desiredState)
{
	__desired_state.reset(new Resolver::SuggestedPackages(desiredState));
	__generate_actions_preview();
}

shared_ptr< const Worker::ActionsPreview > WorkerImpl::getActionsPreview() const
{
	return __actions_preview;
}

map< string, ssize_t > WorkerImpl::getUnpackedSizesPreview() const
{
	map< string, ssize_t > result;

	set< string > changedPackageNames;
	for (size_t i = 0; i < Action::Count; ++i)
	{
		const Resolver::SuggestedPackages& suggestedPackages = (*__actions_preview)[i];
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
		auto oldPackage = __cache->getBinaryPackage(packageName);
		if (oldPackage)
		{
			const shared_ptr< const BinaryVersion >& oldVersion = oldPackage->getInstalledVersion();
			if (oldVersion)
			{
				oldInstalledSize = oldVersion->installedSize;
			}
		}

		// new part
		ssize_t newInstalledSize = 0;
		const shared_ptr< const BinaryVersion >& newVersion = __desired_state->find(packageName)->second.version;
		if (newVersion)
		{
			newInstalledSize = newVersion->installedSize;
		}

		result[packageName] = newInstalledSize - oldInstalledSize;
	}


	return result;
}

pair< size_t, size_t > WorkerImpl::getDownloadSizesPreview() const
{
	auto archivesDirectory = __get_archives_directory();
	size_t totalBytes = 0;
	size_t needBytes = 0;

	static const Action::Type affectedActionTypes[] = {
			Action::Install, Action::Upgrade, Action::Downgrade };
	for (size_t i = 0; i < sizeof(affectedActionTypes) / sizeof(Action::Type); ++i)
	{
		const Resolver::SuggestedPackages& suggestedPackages =
			(*__actions_preview)[affectedActionTypes[i]];

		FORIT(it, suggestedPackages)
		{
			const shared_ptr< const BinaryVersion >& version = it->second.version;
			auto size = version->file.size;

			totalBytes += size;
			needBytes += size; // for start

			auto basename = __get_archive_basename(version);
			auto path = archivesDirectory + "/" + basename;
			if (fs::exists(path))
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

void WorkerImpl::__fill_actions(GraphAndAttributes& gaa)
{
	typedef InnerAction IA;

	// user action - action name from actions preview
	const map< Action::Type, vector< IA::Type > > actionsMapping = {
		{ Action::Install, vector< IA::Type >{ IA::Unpack, IA::Configure } },
		{ Action::Upgrade, vector< IA::Type >{ IA::Remove, IA::Unpack, IA::Configure } },
		{ Action::Downgrade, vector< IA::Type >{ IA::Remove, IA::Unpack, IA::Configure } },
		{ Action::Configure, vector< IA::Type >{ IA::Configure } },
		{ Action::Deconfigure, vector< IA::Type >{ IA::Remove } },
		{ Action::Remove, vector< IA::Type >{ IA::Remove } },
		{ Action::Purge, vector< IA::Type >{ IA::Remove } },
	};

	// packages with indirect upgrades have explicit 'remove' action, so
	// their 'remove' subactions are not fake
	set< string > fakeExceptions;
	{
		auto list = __config->getList("cupt::worker::allow-indirect-upgrade");
		FORIT(it, list)
		{
			fakeExceptions.insert(*it);
		}
	}

	// convert all actions into inner ones
	FORIT(mapIt, actionsMapping)
	{
		const Action::Type& userAction = mapIt->first;
		const vector< IA::Type >& innerActionTypes = mapIt->second;

		FORIT(suggestedPackageIt, (*__actions_preview)[userAction])
		{
			const string& packageName = suggestedPackageIt->first;

			const InnerAction* previousInnerActionPtr = NULL;

			FORIT(innerActionTypeIt, innerActionTypes)
			{
				const IA::Type& innerActionType = *innerActionTypeIt;

				shared_ptr< const BinaryVersion > version;
				if (innerActionType == IA::Remove)
				{
					auto package = __cache->getBinaryPackage(packageName);
					if (package)
					{
						version = package->getInstalledVersion(); // may be undef too in purge-only case
					}
				}
				else
				{
					version = suggestedPackageIt->second.version;
				}
				if (!version)
				{
					auto versionPtr = new BinaryVersion;
					versionPtr->packageName = packageName;
					versionPtr->versionString = __dummy_version_string;
					version.reset(versionPtr);
				}

				InnerAction action;
			    action.version = version;
				action.type = innerActionType;
				action.fake = false;
				if (innerActionType == IA::Remove && innerActionTypes.size() > 1)
				{
					if (!fakeExceptions.count(packageName))
					{
						// this is fake 'remove' action which will be merged
						// with 'unpack' unconditionally later
						action.fake = true;
					}
				}

				auto newVertexPtr = gaa.graph.addVertex(std::move(action));

				if (previousInnerActionPtr)
				{
					// the edge between consecutive actions
					gaa.graph.addEdge(*previousInnerActionPtr, action);
				}
				previousInnerActionPtr = newVertexPtr;
			}
		}
	}
}

void WorkerImpl::__fill_action_dependencies(
		const shared_ptr< const BinaryVersion >& version,
		BinaryVersion::RelationTypes::Type dependencyType, InnerAction::Type actionType,
		Direction::Type direction, const InnerAction& innerAction,
		GraphAndAttributes& gaa, bool debugging)
{
	const set< InnerAction >& verticesMap = gaa.graph.getVertices();

	InnerAction candidateAction;
	candidateAction.type = actionType;

	const RelationLine& relationLine = version->relations[dependencyType];
	FORIT(relationExpressionIt, relationLine)
	{
		auto satisfyingVersions = __cache->getSatisfyingVersions(*relationExpressionIt);

		FORIT(satisfyingVersionIt, satisfyingVersions)
		{
			candidateAction.version = *satisfyingVersionIt;

			// search for the appropriate action in action list
			auto vertexIt = verticesMap.find(candidateAction);
			if (vertexIt == verticesMap.end())
			{
				continue;
			}
			const InnerAction& currentAction = *vertexIt;
			if (innerAction.fake && currentAction.fake)
			{
				continue;
			}

			const InnerAction& masterAction = (direction == Direction::After ? currentAction : innerAction);
			const InnerAction& slaveAction = (direction == Direction::After ? innerAction : currentAction);

			// TODO: uncomment or delete, depending on #582423
			/* bool replacesFound = false;
			if (dependencyType == BinaryVersion::RelationTypes::Conflicts)
			{
				// this is Conflicts, in the case there are appropriate
				// Replaces, the 'remove before' action dependency should not be created
				const RelationLine& replaces = masterAction.version->relations[BinaryVersion::RelationTypes::Replaces];
				FORIT(replacesRelationExpressionIt, replaces)
				{
					auto replacesSatisfyingVersions = cache->getSatisfyingVersions(*replacesRelationExpressionIt);

					auto predicate = std::bind2nd(PointerEqual< const BinaryVersion >(), slaveAction.version);
					if (std::find_if(replacesSatisfyingVersions.begin(), replacesSatisfyingVersions.end(),
							predicate) != replacesSatisfyingVersions.end())
					{
						// yes, found Replaces, skip this action
						replacesFound = true;
						break;
					}
				}
			}
			if (replacesFound)
			{
				continue;
			}
			*/

			gaa.graph.addEdge(slaveAction, masterAction);

			// adding relation to attributes
			vector< GraphAndAttributes::RelationInfoRecord >& relationInfo =
					gaa.attributes[slaveAction][masterAction].relationInfo;
			GraphAndAttributes::RelationInfoRecord record =
					{ dependencyType, *relationExpressionIt, direction == Direction::After };
			relationInfo.push_back(std::move(record));

			if (debugging)
			{
				debug("new action dependency: '%s' -> '%s', reason: '%s: %s'", slaveAction.toString().c_str(),
						masterAction.toString().c_str(), BinaryVersion::RelationTypes::rawStrings[dependencyType],
						relationExpressionIt->toString().c_str());
			}
		}
	}
}

void WorkerImpl::__fill_graph_dependencies(GraphAndAttributes& gaa)
{
	typedef BinaryVersion::RelationTypes RT;

	bool debugging = __config->getBool("debug::worker");

	// fill the actions' dependencies
	const set< InnerAction >& vertices = gaa.graph.getVertices();
	FORIT(vertexIt, vertices)
	{
		const InnerAction& innerAction = *vertexIt;
		const shared_ptr< const BinaryVersion >& version = innerAction.version;
		switch (innerAction.type)
		{
			case InnerAction::Unpack:
			{
				process_unpack:

				// pre-depends must be unpacked before
				__fill_action_dependencies(version, RT::PreDepends, InnerAction::Configure,
						Direction::Before, innerAction, gaa, debugging);
				// conflicts must be unsatisfied before
				__fill_action_dependencies(version, RT::Conflicts, InnerAction::Remove,
						Direction::Before, innerAction, gaa, debugging);
				// breaks must be unsatisfied before (yes, before the unpack)
				__fill_action_dependencies(version, RT::Breaks, InnerAction::Remove,
						Direction::Before, innerAction, gaa, debugging);
			}
				break;
			case InnerAction::Configure:
			{
				// depends must be configured before
				__fill_action_dependencies(version, RT::Depends, InnerAction::Configure,
						Direction::Before, innerAction, gaa, debugging);

				// it has also to be unpacked if the same version was not in state 'unpacked'
				// search for the appropriate unpack action
				auto candidateAction = innerAction;
				candidateAction.type = InnerAction::Unpack;

				if (!vertices.count(candidateAction))
				{
					// add unpack-level prerequisities to this configure action
					goto process_unpack;
				}
			}
				break;
			case InnerAction::Remove:
			{
				// pre-depends must be removed after
				__fill_action_dependencies(version, RT::PreDepends, InnerAction::Remove,
						Direction::After, innerAction, gaa, debugging);
				// depends must be removed after
				__fill_action_dependencies(version, RT::Depends, InnerAction::Remove,
						Direction::After, innerAction, gaa, debugging);
				// conflicts may be satisfied only after
				__fill_action_dependencies(version, RT::Conflicts, InnerAction::Unpack,
						Direction::After, innerAction, gaa, debugging);
				// breaks may be satisfied only after
				__fill_action_dependencies(version, RT::Breaks, InnerAction::Unpack,
						Direction::After, innerAction, gaa, debugging);
				// in the previous case it may happen that package was already unpacked
				// with breaking dependencies already, so there won't be 'unpack' action but just
				// 'configure' one, so set dependency to 'configure' too just in case
				__fill_action_dependencies(version, RT::Breaks, InnerAction::Configure,
						Direction::After, innerAction, gaa, debugging);
			}
		}
	}
}

void WorkerImpl::__check_graph_pre_depends(GraphAndAttributes& gaa)
{
	auto edges = gaa.graph.getEdges();
	FORIT(edgeIt, edges)
	{
		const InnerAction& to = *(edgeIt->first);
		const InnerAction& from = *(edgeIt->second);
		const vector< GraphAndAttributes::RelationInfoRecord >& records =
				gaa.attributes[to][from].relationInfo;

		RelationLine preDependencyRelationExpressions;
		FORIT(recordIt, records)
		{
			if (recordIt->dependencyType == BinaryVersion::RelationTypes::PreDepends)
			{
				preDependencyRelationExpressions.push_back(recordIt->relationExpression);
			}
		}
		if (preDependencyRelationExpressions.empty())
		{
			continue;
		}

		if (gaa.graph.getReachable(to).count(from))
		{
			// bah! the pre-dependency cannot be overridden
			// it is not worker's fail (at least, it shouldn't be)

			auto path = gaa.graph.getPathVertices(to, from);

			vector< string > packageNamesInPath;
			FORIT(pathIt, path)
			{
				packageNamesInPath.push_back((*pathIt)->version->packageName);
			}
			string packageNamesString = join(", ", packageNamesInPath);

			warn("the pre-dependency(ies) '%s' will be broken during the actions, the packages involved: %s",
					preDependencyRelationExpressions.toString().c_str(), packageNamesString.c_str());
		}
	}
}

vector< pair< InnerAction, InnerAction > > __emplace_virtual_edges(
		GraphAndAttributes& gaa, const shared_ptr< const Cache >& cache)
{
	/* here we also adding adding fake-antiupgrades for all packages in the system
	   which stay unmodified for the sake of getting inter-dependencies between
	   the optional dependecies like

	   'abc' depends 'x | y', abc stays unmodified, x goes away, y is going to be installed

	   here, action 'remove x' are dependent on 'install y' one and it gets
	   introduced by

	   'install y' -> 'install abc' <-> 'remove abc' -> 'remove x'
	                  {----------------------------}
	                              <merge>
	   which goes into

	   'install y' -> 'remove x'

	   moreover, we split the installed version into virtual version by one
	   relation expression, so different relation expressions of the same real
	   version don't interact with each other, otherwise we'd get a full cyclic
	   mess
	*/
	vector< pair< InnerAction, InnerAction > > virtualEdges;

	set< string > blacklistedPackageNames;
	// building the black list
	FORIT(vertexIt, gaa.graph.getVertices())
	{
		blacklistedPackageNames.insert(vertexIt->version->packageName);
	}

	auto installedVersions = cache->getInstalledVersions();
	FORIT(installedVersionIt, installedVersions)
	{
		const shared_ptr< const BinaryVersion > installedVersion = *installedVersionIt;
		const string& packageName = installedVersion->packageName;
		if (blacklistedPackageNames.count(packageName))
		{
			continue;
		}

		typedef BinaryVersion::RelationTypes RT;
		static const vector< RT::Type > dependencyTypes = { RT::PreDepends, RT::Depends };
		FORIT(dependencyTypeIt, dependencyTypes)
		{
			FORIT(relationExpressionIt, installedVersion->relations[*dependencyTypeIt])
			{
				shared_ptr< BinaryVersion > virtualVersion(new BinaryVersion);
				virtualVersion->packageName = packageName + " [" + relationExpressionIt->toString() + "]";
				virtualVersion->versionString = installedVersion->versionString;
				virtualVersion->relations[*dependencyTypeIt].push_back(*relationExpressionIt);

				InnerAction from;
				from.version = virtualVersion;
				from.type = InnerAction::Configure;
				from.fake = true;

				InnerAction to;
				to.version = virtualVersion;
				to.type = InnerAction::Remove;
				to.fake = true;

				// we don't add edge here, but add the vertexes to gain dependencies and
				// save the vertexes order
				gaa.graph.addVertex(from);
				gaa.graph.addVertex(to);
				virtualEdges.push_back(std::make_pair(from, to));
			}
		}
	}

	return virtualEdges;
}

void __unite_needed(const shared_ptr< const Config >& config, GraphAndAttributes& gaa,
		const vector< pair< InnerAction, InnerAction > >& virtualEdges)
{
	vector< pair< const InnerAction*, const InnerAction* > > changes;

	// pre-fill the list of downgrades/upgrades vertices
	const set< InnerAction >& vertices = gaa.graph.getVertices();
	FORIT(innerActionIt, vertices)
	{
		if (innerActionIt->type == InnerAction::Remove)
		{
			InnerAction searchAction = *innerActionIt;

			const list< const InnerAction* >& successors = gaa.graph.getSuccessorsFromPointer(&*innerActionIt);
			FORIT(actionPtrIt, successors)
			{
				if ((*actionPtrIt)->type == InnerAction::Unpack &&
					(*actionPtrIt)->version->packageName == innerActionIt->version->packageName)
				{
					changes.push_back(std::make_pair(&*innerActionIt, *actionPtrIt));
				}
			}
		}
	}

	auto moveEdge = [&gaa](const InnerAction& fromPredecessor, const InnerAction& fromSuccessor,
			const InnerAction& toPredecessor, const InnerAction& toSuccessor)
	{
		if (toPredecessor == toSuccessor)
		{
			return;
		}
		GraphAndAttributes::Attribute& toAttribute = gaa.attributes[toPredecessor][toSuccessor];
		GraphAndAttributes::Attribute& fromAttribute = gaa.attributes[fromPredecessor][fromSuccessor];

		// concatenating relationInfo
		toAttribute.relationInfo.insert(toAttribute.relationInfo.end(),
				fromAttribute.relationInfo.begin(), fromAttribute.relationInfo.end());

		if (fromAttribute.multiplied)
		{
			toAttribute.multiplied = true;
		}

		// delete the whole attribute
		gaa.attributes[fromPredecessor].erase(fromSuccessor);

		gaa.graph.deleteEdge(fromPredecessor, fromSuccessor);
		gaa.graph.addEdge(toPredecessor, toSuccessor);
	};

	bool debugging = config->getBool("debug::worker");

	{ // get rid of virtual edges
		FORIT(edgeIt, virtualEdges)
		{
			const InnerAction& from = edgeIt->first;
			const InnerAction& to = edgeIt->second;

			// "multiplying" the dependencies
			const list< const InnerAction* > predecessors = gaa.graph.getPredecessors(from);
			const list< const InnerAction* > successors = gaa.graph.getSuccessors(to);
			FORIT(predecessorVertexPtrIt, predecessors)
			{
				FORIT(successorVertexPtrIt, successors)
				{
					// moving edge attributes too
					moveEdge(**predecessorVertexPtrIt, from, **predecessorVertexPtrIt, **successorVertexPtrIt);
					moveEdge(to, **successorVertexPtrIt, **predecessorVertexPtrIt, **successorVertexPtrIt);
					gaa.attributes[**predecessorVertexPtrIt][**successorVertexPtrIt].multiplied = true;
					if (debugging)
					{
						const string& mediatorPackageName = from.version->packageName;
						debug("multiplied action dependency: '%s' -> '%s', virtual mediator: '%s'",
								(*predecessorVertexPtrIt)->toString().c_str(), (*successorVertexPtrIt)->toString().c_str(),
								mediatorPackageName.c_str());
					}

				}
			}

			gaa.graph.deleteVertex(from);
			gaa.graph.deleteVertex(to);
		}
	}

	{ // unit remove and unpack
		auto mergeExceptionPackageNames = config->getList("cupt::worker::allow-indirect-upgrade");
		FORIT(changeIt, changes)
		{
			const InnerAction& from = *(changeIt->first);
			const InnerAction& to = *(changeIt->second);

			const string& packageName = from.version->packageName;
			if (std::find(mergeExceptionPackageNames.begin(), mergeExceptionPackageNames.end(),
						packageName) == mergeExceptionPackageNames.end())
			{
				const list< const InnerAction* > successors = gaa.graph.getSuccessorsFromPointer(&from);
				const list< const InnerAction* > predecessors = gaa.graph.getPredecessorsFromPointer(&to);
				FORIT(successorPtrIt, successors)
				{
					moveEdge(from, **successorPtrIt, to, **successorPtrIt);
				}
				FORIT(predecessorPtrIt, predecessors)
				{
					moveEdge(**predecessorPtrIt, from, **predecessorPtrIt, to);
				}
				gaa.graph.deleteVertex(from);
			}
		}
	}
}

bool WorkerImpl::__build_actions_graph(GraphAndAttributes& gaa)
{
	if (!__desired_state)
	{
		fatal("worker desired state is not given");
	}

	__fill_actions(gaa);

	// maybe, we have nothing to do?
	if (gaa.graph.getVertices().empty())
	{
		return false;
	}

	auto virtualEdges = __emplace_virtual_edges(gaa, __cache);

	__fill_graph_dependencies(gaa);
	__unite_needed(__config, gaa, virtualEdges);

	if (__config->getBool("debug::worker"))
	{
		auto edges = gaa.graph.getEdges();
		FORIT(edgeIt, edges)
		{
			debug("the present action dependency: '%s' -> '%s'",
					edgeIt->first->toString().c_str(), edgeIt->second->toString().c_str());
		}
	}

	__check_graph_pre_depends(gaa);

	return true;
}

void __split_heterogeneous_actions(
		vector< vector< InnerAction > >& actionGroups, GraphAndAttributes& gaa)
{
	vector< vector< InnerAction > > newActionGroups;

	FORIT(actionGroupIt, actionGroups)
	{
		// all the actions in the group will have the same type by algorithm
		const vector< InnerAction >& actionGroup = *actionGroupIt;
		if (actionGroup.size() > 1)
		{
			// multiple actions at once

			// firstly, we need to cope with conflicts at early stage, so we build a mini-graph
			internal::Graph< InnerAction > miniActionGraph;

			vector< pair< const InnerAction*, const InnerAction* > > edges;
			// pre-fill edges and fill vertices
			FORIT(actionIt, actionGroup)
			{
				const list< const InnerAction* >& predecessors = gaa.graph.getPredecessors(*actionIt);
				const list< const InnerAction* >& successors = gaa.graph.getSuccessors(*actionIt);
				FORIT(predecessorPtrIt, predecessors)
				{
					edges.push_back(std::make_pair(*predecessorPtrIt, &*actionIt));
				}
				FORIT(successorPtrIt, successors)
				{
					edges.push_back(std::make_pair(&*actionIt, *successorPtrIt));
				}
				miniActionGraph.addVertex(*actionIt);
			}
			// filter edges
			const set< InnerAction >& allowedVertices = miniActionGraph.getVertices();
			FORIT(edgeIt, edges)
			{
				const InnerAction& from = *(edgeIt->first);
				const InnerAction& to = *(edgeIt->second);
				if (!allowedVertices.count(from) || !allowedVertices.count(to))
				{
					continue; // edge lies outside our mini graph
				}

				const GraphAndAttributes::Attribute& attribute = gaa.attributes[from][to];
				if (attribute.multiplied)
				{
					continue;
				}

				bool suitable = false;
				if (attribute.relationInfo.empty())
				{
					suitable = true;
				}
				else
				{
					FORIT(recordIt, attribute.relationInfo)
					{
						if (recordIt->dependencyType != BinaryVersion::RelationTypes::Breaks &&
							(!recordIt->reverse || recordIt->dependencyType == BinaryVersion::RelationTypes::Conflicts))
						{
							suitable = true;
							break;
						}
					}
				}
				if (suitable)
				{
					miniActionGraph.addEdge(from, to);
				}
			}

			auto actionSubgroupsSorted =
					miniActionGraph.topologicalSortOfStronglyConnectedComponents();

			FORIT(actionSubgroupIt, actionSubgroupsSorted)
			{
				vector< InnerAction >& actionSubgroup = *actionSubgroupIt;
				if (actionSubgroup.size() > 1)
				{
					// only circular configures allowed
					FORIT(actionIt, actionSubgroup)
					{
						if (actionIt->type != InnerAction::Configure)
						{
							// ooh, mixed circular dependency? no-go
							vector< string > actionStrings;
							FORIT(it, actionSubgroup)
							{
								actionStrings.push_back(it->toString());
							}
							auto suggestionString = __("you may try to work around by adding one or more affected packages "
									"to the 'cupt::worker::allow-indirect-upgrade' configuration variable");
							fatal("unable to schedule circular actions '%s', %s",
									join(", ", actionStrings).c_str(), suggestionString.c_str());
						}
					}
				}

				// always set forcing all dependencies
				// dpkg requires to pass both --force-depends and --force-breaks to achieve it
				actionSubgroup[0].dpkgFlags = " --force-depends --force-breaks";

				newActionGroups.push_back(actionSubgroup);
			}
		}
		else
		{
			newActionGroups.push_back(actionGroup);
		}
	}

	newActionGroups.swap(actionGroups);
}

void __move_unpacks_to_configures (vector< vector< InnerAction > >& actionGroups,
		const GraphAndAttributes& gaa)
{
	auto move = [&gaa, &actionGroups](const InnerAction::Type actionType)
	{
		FORIT(actionGroupIt, actionGroups)
		{
			bool suitable = true;
			FORIT(actionIt, *actionGroupIt)
			{
				if (actionIt->type != actionType)
				{
					// some other action, including heterogeneous variant
					suitable = false;
					break;
				}
			}
			if (!suitable)
			{
				continue;
			}

			list< const list< const InnerAction* >* > disallowedPairs;
			FORIT(actionIt, *actionGroupIt)
			{
				const list< const InnerAction* >& pairs = (actionType == InnerAction::Unpack) ?
						gaa.graph.getSuccessors(*actionIt) : gaa.graph.getPredecessors(*actionIt);
				disallowedPairs.push_back(&pairs);
			}
			const vector< InnerAction > actionGroup = *actionGroupIt;

			// ok, try to move it as right as possible
			auto nextIt = actionGroupIt + 1;
			while (nextIt != actionGroups.end())
			{
				vector< InnerAction >& next = *nextIt;

				FORIT(nextActionIt, next)
				{
					FORIT(listPtrIt, disallowedPairs)
					{
						FORIT(pairPtrIt, **listPtrIt)
						{
							if (*nextActionIt == **pairPtrIt)
							{
								// cannot move
								goto last_try;
							}
						}
					}
				}

				// move!
				vector< InnerAction >& current = *(nextIt - 1);
				current.swap(next);

				++nextIt;
			}

			last_try:
			;
		}
	};

	move(InnerAction::Unpack);
	std::reverse(actionGroups.begin(), actionGroups.end());
	move(InnerAction::Configure);
	std::reverse(actionGroups.begin(), actionGroups.end());
}

map< string, pair< download::Manager::DownloadEntity, string > > WorkerImpl::__prepare_downloads()
{
	auto archivesDirectory = __get_archives_directory();

	if (!__config->getBool("cupt::worker::simulate"))
	{
		string partialDirectory = archivesDirectory + __partial_directory_suffix;
		if (! fs::exists(partialDirectory))
		{
			if (mkdir(partialDirectory.c_str(), 0755) == -1)
			{
				fatal("unable to create partial directory '%s': EEE", partialDirectory.c_str());
			}
		}
	}

	map< string, pair< download::Manager::DownloadEntity, string > > downloads;

	internal::DebdeltaHelper debdeltaHelper;

	static const vector< Action::Type > actions = { Action::Install, Action::Upgrade, Action::Downgrade };
	FORIT(actionIt, actions)
	{
		const Resolver::SuggestedPackages& suggestedPackages = (*__actions_preview)[*actionIt];
		FORIT(it, suggestedPackages)
		{
			const shared_ptr< const BinaryVersion >& version = it->second.version;

			const string& packageName = version->packageName;
			const string& versionString = version->versionString;

			auto downloadInfo = version->getDownloadInfo();

			// we need at least one real URI
			if (downloadInfo.empty())
			{
				fatal("no available download URIs for %s %s", packageName.c_str(), versionString.c_str());
			}

			// paths
			auto basename = __get_archive_basename(version);
			auto downloadPath = archivesDirectory + __partial_directory_suffix + '/' + basename;
			auto targetPath = archivesDirectory + '/' + basename;

			// exclude from downloading packages that are already present
			if (fs::exists(targetPath) &&
				version->file.hashSums.verify(targetPath))
			{
				continue;
			}

			download::Manager::DownloadEntity downloadEntity;

			const shared_ptr< const ReleaseInfo > release = version->sources[0].release;
			string longAliasTail = sf("%s/%s %s %s", release->codename.c_str(),
						release->component.c_str(), packageName.c_str(), versionString.c_str());
			FORIT(it, downloadInfo)
			{
				string uri = it->baseUri + '/' + it->directory + '/' + version->file.name;


				string shortAlias = packageName;
				string longAlias = it->baseUri + ' ' + longAliasTail;

				downloadEntity.extendedUris.push_back(
						download::Manager::ExtendedUri(uri, shortAlias, longAlias));
			}
			{
				auto debdeltaDownloadInfo = debdeltaHelper.getDownloadInfo(version, __cache);
				FORIT(it, debdeltaDownloadInfo)
				{
					const string& uri = it->uri;
					string longAlias = it->baseUri + ' ' + longAliasTail;

					downloadEntity.extendedUris.push_back(
							download::Manager::ExtendedUri(uri, packageName, longAlias));
				}
			}

			// caution: targetPath and downloadEntity.targetPath are different
			downloadEntity.targetPath = downloadPath;
			downloadEntity.size = version->file.size;

			downloadEntity.postAction = [version, downloadPath, targetPath]() -> string
			{
				if (!fs::exists(downloadPath))
				{
					return __("unable to find downloaded file");
				}
				if (!version->file.hashSums.verify(downloadPath))
				{
					unlink(downloadPath.c_str()); // intentionally ignore errors if any
					return __("hash sums mismatch");
				}

				return fs::move(downloadPath, targetPath);
			};

			auto downloadValue = std::make_pair(std::move(downloadEntity), targetPath);
			downloads.insert(std::make_pair(packageName, downloadValue));
		}
	}

	return downloads;
}

vector< Changeset > __split_action_groups_into_changesets(
		const vector< vector< InnerAction > >& actionGroups,
		const map< string, pair< download::Manager::DownloadEntity, string > >& downloads)
{
	vector< Changeset > result;
	Changeset changeset;
	set< string > unpackedPackageNames;

	FORIT(actionGroupIt, actionGroups)
	{
		auto actionType = (*actionGroupIt)[0].type;

		FORIT(actionIt, *actionGroupIt)
		{
			const string& packageName = actionIt->version->packageName;
			if (actionType == InnerAction::Unpack)
			{
				unpackedPackageNames.insert(packageName);

				auto downloadEntryIt = downloads.find(packageName);
				if (downloadEntryIt != downloads.end())
				{
					// don't need package name anymore
					changeset.downloads.push_back(downloadEntryIt->second);
				}
			}
			else if (actionType == InnerAction::Configure)
			{
				unpackedPackageNames.erase(packageName);
			}
		}

		changeset.actionGroups.push_back(*actionGroupIt);

		if (unpackedPackageNames.empty())
		{
			// all unpacked packages are configured, the end of changeset
			result.push_back(std::move(changeset));

			changeset.actionGroups.clear();
			changeset.downloads.clear();
		}
	}

	if (!unpackedPackageNames.empty())
	{
		vector< string > unconfiguredPackageNames;
		std::copy(unpackedPackageNames.begin(), unpackedPackageNames.end(),
				std::back_inserter(unconfiguredPackageNames));
		fatal("internal error: packages stay unconfigured: '%s'",
				join(" ", unconfiguredPackageNames).c_str());
	}

	return result;
}

size_t __get_download_amount(const Changeset& changeset)
{
	const vector< pair< download::Manager::DownloadEntity, string > >& source = changeset.downloads;

	size_t amount = 0;
	FORIT(it, source)
	{
		amount += it->first.size;
	}

	return amount;
}

size_t __get_max_download_amount(const vector< Changeset >& changesets, bool debugging)
{
	size_t result = 0;

	vector< string > amounts; // for debugging
	FORIT(changesetIt, changesets)
	{
		auto amount = __get_download_amount(*changesetIt);
		if (amount > result)
		{
			result = amount;
		}
		if (debugging)
		{
			amounts.push_back(humanReadableSizeString(amount));
		}
	}
	if (debugging)
	{
		debug("the changeset download amounts: maximum: %s, all: %s",
				humanReadableSizeString(result).c_str(), join(", ", amounts).c_str());
	}

	return result;
}

vector< Changeset > WorkerImpl::__get_changesets(GraphAndAttributes& gaa,
		const map< string, pair< download::Manager::DownloadEntity, string > >& downloads)
{
	auto debugging = __config->getBool("debug::worker");
	size_t archivesSpaceLimit = __config->getInteger("cupt::worker::archives-space-limit");
	size_t archivesSpaceLimitTries = __config->getInteger("cupt::worker::archives-space-limit::tries");

	bool ok = false;
	size_t minMaxDownloadAmount = -1;

	vector< Changeset > changesets;

	for (size_t i = 0; i < archivesSpaceLimitTries; ++i)
	{
		auto actionGroups = gaa.graph.topologicalSortOfStronglyConnectedComponents();
		__move_unpacks_to_configures(actionGroups, gaa);
		__split_heterogeneous_actions(actionGroups, gaa);

		changesets = __split_action_groups_into_changesets(actionGroups, downloads);

		if (!archivesSpaceLimit)
		{
			ok = true;
			break;
		}

		auto maxDownloadAmount = __get_max_download_amount(changesets, debugging);
		if (minMaxDownloadAmount > maxDownloadAmount)
		{
			minMaxDownloadAmount = maxDownloadAmount;
		}
		if (debugging)
		{
			debug("the changeset download amounts: minimum maximum: %s",
					humanReadableSizeString(minMaxDownloadAmount).c_str());
		}
		if (maxDownloadAmount <= archivesSpaceLimit)
		{
			ok = true;
			break;
		}
	}

	if (ok)
	{
		auto doesDownloadAmountFit = [&archivesSpaceLimit](const size_t amount) -> bool
		{
			return !archivesSpaceLimit || amount <= archivesSpaceLimit;
		};

		vector< Changeset > newChangesets = { Changeset() }; // with one empty element
		FORIT(changesetIt, changesets)
		{
			const Changeset& nextChangeset = *changesetIt;
			Changeset& currentChangeset = *(newChangesets.rbegin());

			auto currentDownloadAmount = __get_download_amount(currentChangeset);
			auto nextDownloadAmount = __get_download_amount(nextChangeset);
			if (doesDownloadAmountFit(currentDownloadAmount + nextDownloadAmount))
			{
				// merge!
				currentChangeset.actionGroups.insert(currentChangeset.actionGroups.end(),
						nextChangeset.actionGroups.begin(), nextChangeset.actionGroups.end());
				currentChangeset.downloads.insert(currentChangeset.downloads.end(),
						nextChangeset.downloads.begin(), nextChangeset.downloads.end());
			}
			else
			{
				newChangesets.push_back(nextChangeset);
			}
		}
		changesets.swap(newChangesets);
	}
	else
	{
		// we failed to fit in limit
		fatal("unable to fit in archives space limit '%zu', best try is '%zu'",
				archivesSpaceLimit, minMaxDownloadAmount);
	}

	return changesets;
}

void WorkerImpl::__run_external_command(const string& command, const string& errorId)
{
	if (__config->getBool("cupt::worker::simulate"))
	{
		simulate("running command '%s'", command.c_str());
	}
	else
	{
		// invoking command
		auto result = ::system(command.c_str());
		const char* id = (errorId.empty() ? command.c_str() : errorId.c_str());
		if (result == -1)
		{
			fatal("'%s': system() failed: EEE", id);
		}
		if (WIFEXITED(result))
		{
			auto exitCode = WEXITSTATUS(result);
			if (exitCode != 0)
			{
				fatal("'%s' returned non-zero status: %u", id, exitCode);
			}
		}
		else if (WIFSIGNALED(result))
		{
			fatal("'%s' was stopped by signal %u'", id, WTERMSIG(result));
		}
		else
		{
			fatal("'%s' was terminated due to unknown reason", id);
		}
	}
}

void WorkerImpl::__run_dpkg_command(const string& flavor, const string& command, const string& alias)
{
	auto errorId = sf(__("dpkg '%s' action '%s'"), flavor.c_str(), alias.c_str());
	__run_external_command(command, errorId);
}

void WorkerImpl::__clean_downloads(const Changeset& changeset)
{
	internal::Lock archivesLock(__config, __get_archives_directory() + "/lock");

	bool simulating = __config->getBool("cupt::worker::simulate");
	FORIT(it, changeset.downloads)
	{
		const char* targetPath = it->second.c_str();
		if (simulating)
		{
			simulate("removing archive '%s'", targetPath);
		}
		else
		{
			if (unlink(targetPath) == -1)
			{
				fatal("unable to remove file '%s': EEE", targetPath);
			}
		}
	}
}

void WorkerImpl::__do_dpkg_pre_actions()
{
	auto commands = __config->getList("dpkg::pre-invoke");
	FORIT(commandIt, commands)
	{
		__run_dpkg_command("post", *commandIt, *commandIt);
	}
}

string WorkerImpl::__generate_input_for_preinstall_v2_hooks(
		const vector< vector< InnerAction > >& actionGroups)
{
	// all hate undocumented formats...
	string result = "VERSION 2\n";

	{ // writing out a configuration
		auto printKeyValue = [&result](const string& key, const string& value)
		{
			if (!value.empty())
			{
				result += (key + "=" + value + "\n");
			}
		};

		{
			auto regularKeys = __config->getScalarOptionNames();
			FORIT(keyIt, regularKeys)
			{
				printKeyValue(*keyIt, __config->getString(*keyIt));
			}
		}
		{
			auto listKeys = __config->getListOptionNames();
			FORIT(keyIt, listKeys)
			{
				const string& key = *keyIt;
				auto values = __config->getList(key);
				FORIT(valueIt, values)
				{
					printKeyValue(key + "::", *valueIt);
				}
			}
		}

		result += "\n";
	}

	auto archivesDirectory = __get_archives_directory();
	FORIT(actionGroupIt, actionGroups)
	{
		auto actionType = actionGroupIt->begin()->type;

		FORIT(actionIt, *actionGroupIt)
		{
			const shared_ptr< const BinaryVersion >& version = actionIt->version;
			const string& packageName = version->packageName;

			string oldVersionString = "-";
			auto oldPackage = __cache->getBinaryPackage(packageName);
			if (oldPackage)
			{
				auto installedVersion = oldPackage->getInstalledVersion();
				if (installedVersion)
				{
					oldVersionString = installedVersion->versionString;
				}
			}
			string newVersionString = (actionType == InnerAction::Remove ? "-" : version->versionString);

			string compareVersionStringsSign;
			if (oldVersionString == "-")
			{
				compareVersionStringsSign = "<";
			}
			else if (newVersionString == "-")
			{
				compareVersionStringsSign = ">";
			}
			else
			{
				auto comparisonResult = compareVersionStrings(oldVersionString, newVersionString);
				switch (comparisonResult)
				{
					case -1: compareVersionStringsSign = "<"; break;
					case  0: compareVersionStringsSign = "="; break;
					case  1: compareVersionStringsSign = ">"; break;
				}
			}

			string path;
			switch (actionType)
			{
				case InnerAction::Configure:
				{
					path = "**CONFIGURE**";
				}
				break;
				case InnerAction::Remove:
				{
					path = "**REMOVE**";
				}
				break;
				case InnerAction::Unpack:
				{
					path = archivesDirectory + "/" + __get_archive_basename(version);
				}
			}
			result += sf("%s %s %s %s %s\n", packageName.c_str(), oldVersionString.c_str(),
					compareVersionStringsSign.c_str(), newVersionString.c_str(), path.c_str());
		}
	}

	// strip last "\n", because apt-listchanges cannot live with it somewhy
	result.erase(result.end() - 1);

	return result;
}

void WorkerImpl::__do_dpkg_pre_packages_actions(const vector< vector< InnerAction > >& actionGroups)
{
	auto archivesDirectory = __get_archives_directory();
	auto commands = __config->getList("dpkg::pre-install-pkgs");
	FORIT(commandIt, commands)
	{
		string command = *commandIt;
		string commandBinary = command;

		auto spaceOffset = commandBinary.find(' ');
		if (spaceOffset != string::npos)
		{
			commandBinary.resize(spaceOffset);
		}

		string commandInput;
		auto versionOfInput = __config->getInteger(
				string("dpkg::tools::options::") + commandBinary + "::version");
		if (versionOfInput == 2)
		{
			commandInput = __generate_input_for_preinstall_v2_hooks(actionGroups);
		}
		else
		{
			// new debs are pulled to command through STDIN, one by line
			FORIT(actionGroupIt, actionGroups)
			{
				FORIT(actionIt, *actionGroupIt)
				{
					if (actionIt->type == InnerAction::Unpack)
					{
						auto debPath = archivesDirectory + "/" + __get_archive_basename(actionIt->version);
						commandInput += debPath;
						commandInput += "\n";
					}
				}
			}
		}

		string alias = command;
		command = string("echo '") + commandInput + "' | " + command;
		__run_dpkg_command("pre", command, alias);
	}
}

void WorkerImpl::__do_dpkg_post_actions()
{
	auto commands = __config->getList("dpkg::post-invoke");
	FORIT(commandIt, commands)
	{
		__run_dpkg_command("post", *commandIt, *commandIt);
	}
}

void __change_auto_status(WorkerImpl& workerImpl, InnerAction::Type actionType,
		const vector< InnerAction >& actionGroup)
{
	if (actionType == InnerAction::Configure)
	{
		return;
	}

	bool targetStatus = (actionType == InnerAction::Unpack); // will be false for removals

	const Resolver::SuggestedPackages& suggestedPackages =
			(*(workerImpl.getActionsPreview()))[targetStatus ? Worker::Action::Markauto : Worker::Action::Unmarkauto];

	FORIT(actionIt, actionGroup)
	{
		const string& packageName = actionIt->version->packageName;
		if (suggestedPackages.count(packageName))
		{
			workerImpl.markAsAutomaticallyInstalled(packageName, targetStatus);
		}
	}
}

void WorkerImpl::markAsAutomaticallyInstalled(const string& packageName, bool targetStatus)
{
	auto simulating = __config->getBool("cupt::worker::simulate");

	if (simulating)
	{
		string prefix = targetStatus ?
					__("marking as automatically installed") : __("marking as manually installed");
		simulate("%s: %s", prefix.c_str(), packageName.c_str());
	}
	else
	{
		if (targetStatus)
		{
			__auto_installed_package_names.insert(packageName);
		}
		else
		{
			__auto_installed_package_names.erase(packageName);
		}
		auto extendedInfoPath = __cache->getPathOfExtendedStates();
		auto tempPath = extendedInfoPath + ".cupt.tmp";

		{
			string errorString;
			File tempFile(tempPath, "w", errorString);
			if (!errorString.empty())
			{
				fatal("unable to open temporary file '%s': %s", tempPath.c_str(), errorString.c_str());
			}

			// filling new info
			FORIT(packageNameIt, __auto_installed_package_names)
			{
				tempFile.put(sf("Package: %s\nAuto-Installed: 1\n\n", packageNameIt->c_str()));
			}
		}

		auto moveResult = fs::move(tempPath, extendedInfoPath);
		if (!moveResult.empty())
		{
			fatal("unable to renew extended states file: %s", moveResult.c_str());
		}
	}
}

void WorkerImpl::__do_downloads(const vector< pair< download::Manager::DownloadEntity, string > >& downloads,
		const shared_ptr< download::Progress >& downloadProgress)
{
	// don't bother ourselves with download preparings if nothing to download
	if (!downloads.empty())
	{
		auto archivesDirectory = __get_archives_directory();

		string downloadResult;
		{
			Lock lock(__config, archivesDirectory + "/lock");

			uint64_t totalDownloadSize = 0;
			FORIT(it, downloads)
			{
				totalDownloadSize += it->first.size;
			}
			downloadProgress->setTotalEstimatedSize(totalDownloadSize);

			vector< download::Manager::DownloadEntity > params;
			FORIT(it, downloads)
			{
				params.push_back(it->first);
			}

			{
				download::Manager downloadManager(__config, downloadProgress);
				downloadResult = downloadManager.download(params);
			} // make sure that download manager is already destroyed at this point, before lock is released
		}

		if (!downloadResult.empty())
		{
			fatal("there were download errors");
		}
	}
}

void WorkerImpl::changeSystem(const shared_ptr< download::Progress >& downloadProgress)
{
	auto debugging = __config->getBool("debug::worker");
	auto archivesSpaceLimit = __config->getInteger("cupt::worker::archives-space-limit");
	auto downloadOnly = __config->getBool("cupt::worker::download-only");

	auto preDownloads = __prepare_downloads();
	if (downloadOnly)
	{
		// download all now
		vector< pair< download::Manager::DownloadEntity, string > > downloads;
		FORIT(preDownloadIt, preDownloads)
		{
			downloads.push_back(preDownloadIt->second);
		}
		__do_downloads(downloads, downloadProgress);
		return;
	};

	vector< Changeset > changesets;
	{
		GraphAndAttributes gaa;
		if (!__build_actions_graph(gaa))
		{
			return; // exit when nothing to do
		}

		changesets = __get_changesets(gaa, preDownloads);
	}

	// doing or simulating the actions
	auto dpkgBinary = __config->getString("dir::bin::dpkg");
	{
		auto dpkgOptions = __config->getList("dpkg::options");
		FORIT(optionIt, dpkgOptions)
		{
			dpkgBinary += " ";
			dpkgBinary += *optionIt;
		}
	}

	auto deferTriggers = __config->getBool("cupt::worker::defer-triggers");

	__do_dpkg_pre_actions();

	auto archivesDirectory = __get_archives_directory();
	auto purging = __config->getBool("cupt::worker::purge");
	FORIT(changesetIt, changesets)
	{
		const Changeset& changeset = *changesetIt;

		if (debugging)
		{
			debug("started changeset");
		}
		/* usually, all downloads are done before any install actions (first
		   and only changeset) however, if 'cupt::worker::archives-space-limit'
		   is turned on this is no longer the case, and we will do downloads/installs
		   by portions ("changesets") */
		__do_downloads(changeset.downloads, downloadProgress);

		__do_dpkg_pre_packages_actions(changeset.actionGroups);

		FORIT(actionGroupIt, changeset.actionGroups)
		{
			// all actions within one group have the same action name by algorithm
			InnerAction::Type actionType = (*actionGroupIt)[0].type;

			string actionName;
			switch (actionType)
			{
				case InnerAction::Remove:
					actionName = purging ? "purge" : "remove";
					break;
				case InnerAction::Unpack:
					actionName = "unpack";
					break;
				case InnerAction::Configure:
					actionName = "configure";
			}

			__change_auto_status(*this, actionType, *actionGroupIt);

			{ // dpkg actions
				auto dpkgCommand = dpkgBinary + " --" + actionName;
				if (deferTriggers)
				{
					dpkgCommand += " --no-triggers";
				}
				// add necessary options if requested
				dpkgCommand += (*actionGroupIt)[0].dpkgFlags;

				/* the workaround for a dpkg bug #558151

				   dpkg performs some IMHO useless checks for programs in PATH
				   which breaks some upgrades of packages that contains important programs */
				dpkgCommand += " --force-bad-path";

				FORIT(actionIt, *actionGroupIt)
				{
					string actionExpression;
					if (actionType == InnerAction::Unpack)
					{
						const shared_ptr< const BinaryVersion > version = actionIt->version;
						actionExpression = archivesDirectory + '/' + __get_archive_basename(version);
					}
					else
					{
						actionExpression = actionIt->version->packageName;
					}
					dpkgCommand += " ";
					dpkgCommand += actionExpression;
				}
				if (debugging)
				{
					vector< string > stringifiedVersions;
					const string& dpkgFlags = (*actionGroupIt)[0].dpkgFlags;
					FORIT(actionIt, *actionGroupIt)
					{
						const shared_ptr< const BinaryVersion > version = actionIt->version;
						stringifiedVersions.push_back(version->packageName + '_' + version->versionString);
					}
					debug("%s %s %s", actionName.c_str(), dpkgFlags.c_str(),
							join(" ", stringifiedVersions).c_str());
				}
				__run_external_command(dpkgCommand);
			};
		}
		if (deferTriggers)
		{
			// triggers were not processed during actions perfomed before, do it now at once
			string command = dpkgBinary + " --triggers-only --pending";
			if (debugging)
			{
				debug("running triggers");
			}
			__run_dpkg_command("triggers", command, command);
		}

		if (archivesSpaceLimit)
		{
			__clean_downloads(changeset);
		}
		if (debugging)
		{
			debug("finished changeset");
		}
	}

	__do_dpkg_post_actions();
}


string getDownloadPath(const string& targetPath)
{
	return fs::dirname(targetPath) + __partial_directory_suffix +
			"/" + fs::filename(targetPath);
}

string getUriBasename(const string& uri)
{
	auto slashPosition = uri.rfind('/');
	if (slashPosition != string::npos)
	{
		return uri.substr(slashPosition + 1);
	}
	else
	{
		return uri;
	}
}

string getFilenameExtension(const string& source)
{
	auto position = source.find_last_of("./");
	if (position != string::npos && source[position] == '.')
	{
		return source.substr(position);
	}
	else
	{
		return string();
	}
};

std::function< string () > generateMovingSub(const string& downloadPath, const string& targetPath)
{
	return [downloadPath, targetPath]() -> string
	{
		return fs::move(downloadPath, targetPath);
	};
};

bool generateUncompressingSub(const download::Uri& uri, const string& downloadPath,
		const string& targetPath, std::function< string () >& sub)
{
	auto filenameExtension = getFilenameExtension(downloadPath);

	// checking and preparing unpackers
	if (filenameExtension == ".lzma" || filenameExtension == ".bz2" || filenameExtension == ".gz")
	{
		string uncompressorName;
		if (filenameExtension == ".lzma")
		{
			uncompressorName = "unlzma";
		}
		else if (filenameExtension == ".bz2")
		{
			uncompressorName = "bunzip2";
		}
		else if (filenameExtension == ".gz")
		{
			uncompressorName = "gunzip";
		}
		else
		{
			fatal("internal error: extension '%s' has no uncompressor", filenameExtension.c_str());
		}

		if (::system(sf("which %s >/dev/null", uncompressorName.c_str()).c_str()))
		{
			warn("'%s' uncompressor is not available, not downloading '%s'",
					uncompressorName.c_str(), string(uri).c_str());
			return false;
		}

		sub = [uncompressorName, downloadPath, targetPath]() -> string
		{
			auto uncompressingResult = ::system(sf("%s %s -c > %s",
					uncompressorName.c_str(), downloadPath.c_str(), targetPath.c_str()).c_str());
			// anyway, remove the compressed file, ignoring errors if any
			unlink(downloadPath.c_str());
			if (uncompressingResult)
			{
				return sf(__("failed to uncompress '%s', '%s' returned error %d"),
						downloadPath.c_str(), uncompressorName.c_str(), uncompressingResult);
			}
			return string(); // success
		};
		return true;
	}
	else if (filenameExtension.empty())
	{
		// no extension
		sub = generateMovingSub(downloadPath, targetPath);
		return true;
	}
	else
	{
		warn("unknown file extension '%s', not downloading '%s'",
					filenameExtension.c_str(), string(uri).c_str());
		return false;
	}
};

bool WorkerImpl::__update_release(download::Manager& downloadManager,
		const Cache::IndexEntry& indexEntry, bool& releaseFileChanged)
{
	auto targetPath = __cache->getPathOfReleaseList(indexEntry);

	// we'll check hash sums of local file before and after to
	// determine do we need to clean partial indexes
	//
	HashSums hashSums; // empty now
	hashSums[HashSums::MD5] = "0"; // won't match for sure
	if (fs::exists(targetPath))
	{
		// the Release file already present
		hashSums.fill(targetPath);
	}
	releaseFileChanged = false; // until proved otherwise later

	// downloading Release file
	auto alias = indexEntry.distribution + ' ' + "Release";

	auto uri = __cache->getDownloadUriOfReleaseList(indexEntry);
	auto downloadPath = getDownloadPath(targetPath);

	{
		download::Manager::DownloadEntity downloadEntity;

		download::Manager::ExtendedUri extendedUri(download::Uri(uri),
				alias, indexEntry.uri + ' ' + alias);

		downloadEntity.extendedUris.push_back(std::move(extendedUri));
		downloadEntity.targetPath = downloadPath;
		downloadEntity.postAction = generateMovingSub(downloadPath, targetPath);
		downloadEntity.size = (size_t)-1;

		auto downloadResult = downloadManager.download(
				vector< download::Manager::DownloadEntity >{ downloadEntity });
		if (!downloadResult.empty())
		{
			return false;
		}
	}

	releaseFileChanged = !hashSums.verify(targetPath);

	// downloading signature for Release file
	auto signatureUri = uri + ".gpg";
	auto signatureTargetPath = targetPath + ".gpg";
	auto signatureDownloadPath = downloadPath + ".gpg";

	auto signatureAlias = alias + ".gpg";

	auto signaturePostAction = generateMovingSub(signatureDownloadPath, signatureTargetPath);

	bool simulating = __config->getBool("cupt::worker::simulate");
	if (!simulating and !__config->getBool("cupt::update::keep-bad-signatures"))
	{
		// if we have to check signature prior to moving to canonical place
		// (for compatibility with APT tools) and signature check failed,
		// delete the downloaded file
		auto oldSignaturePostAction = signaturePostAction;
		signaturePostAction = [oldSignaturePostAction, alias, targetPath, signatureTargetPath, &__config]() -> string
		{
			auto moveError = oldSignaturePostAction();
			if (!moveError.empty())
			{
				return moveError;
			}

			if (!Cache::verifySignature(__config, targetPath))
			{
				if (unlink(signatureTargetPath.c_str()) == -1)
				{
					warn("unable to delete file '%s': EEE", signatureTargetPath.c_str());
				}
				warn("signature verification for '%s' failed", alias.c_str());
			}
			return string();
		};
	}

	{
		download::Manager::DownloadEntity downloadEntity;

		download::Manager::ExtendedUri extendedUri(download::Uri(signatureUri),
				signatureAlias, indexEntry.uri + ' ' + signatureAlias);

		downloadEntity.extendedUris.push_back(std::move(extendedUri));
		downloadEntity.targetPath = signatureDownloadPath;
		downloadEntity.postAction = signaturePostAction;
		downloadEntity.size = (size_t)-1;

		auto downloadResult = downloadManager.download(
				vector< download::Manager::DownloadEntity >{ downloadEntity });
		if (!downloadResult.empty())
		{
			return false;
		}
	}

	return true;
}

ssize_t WorkerImpl::__get_uri_priority(const string& uri)
{
	auto extension = getFilenameExtension(uri);
	if (extension.empty())
	{
		extension = "uncompressed";
	}
	else if (extension[0] == '.') // will be true probably in all cases
	{
		extension = extension.substr(1); // remove starting '.' if exist
	}
	auto variableName = string("cupt::update::compression-types::") +
			extension + "::priority";
	return __config->getInteger(variableName);
}

bool WorkerImpl::__update_index(download::Manager& downloadManager,
		const Cache::IndexEntry& indexEntry, bool releaseFileChanged, bool& indexFileChanged)
{
	// downloading Packages/Sources
	auto targetPath = __cache->getPathOfIndexList(indexEntry);
	auto downloadInfo = __cache->getDownloadInfoOfIndexList(indexEntry);

	indexFileChanged = true;

	// checking maybe there is no difference between the local and the remote?
	bool simulating = __config->getBool("cupt::worker::simulate");
	if (!simulating && fs::exists(targetPath))
	{
		FORIT(downloadRecordIt, downloadInfo)
		{
			if (downloadRecordIt->hashSums.verify(targetPath))
			{
				// yeah, really
				indexFileChanged = false;
				return true;
			}
		}
	}

	{ // sort download files by priority and size
		auto comparator = [this](const Cache::IndexDownloadRecord& left, const Cache::IndexDownloadRecord& right)
		{
			auto leftPriority = this->__get_uri_priority(left.uri);
			auto rightPriority = this->__get_uri_priority(right.uri);
			if (leftPriority == rightPriority)
			{
				return left.size < right.size;
			}
			else
			{
				return (leftPriority > rightPriority);
			}
		};
		std::sort(downloadInfo.begin(), downloadInfo.end(), comparator);
	}

	auto baseDownloadPath = getDownloadPath(targetPath);
	string downloadError;
	FORIT(downloadRecordIt, downloadInfo)
	{
		const string& uri = downloadRecordIt->uri;
		auto downloadPath = baseDownloadPath + getFilenameExtension(uri);

		std::function< string () > uncompressingSub;
		if (!generateUncompressingSub(uri, downloadPath, targetPath, uncompressingSub))
		{
			continue;
		}

		auto alias = indexEntry.distribution + '/' + indexEntry.component +
				' ' + getUriBasename(uri);

		if (!simulating)
		{
			// here we check for outdated dangling indexes in partial directory
			if (releaseFileChanged && fs::exists(downloadPath))
			{
				if (unlink(downloadPath.c_str()) == -1)
				{
					warn("unable to remove outdated partial index file '%s': EEE",
							downloadPath.c_str());
				}
			}
		}

		download::Manager::DownloadEntity downloadEntity;

		download::Manager::ExtendedUri extendedUri(download::Uri(uri),
				alias, indexEntry.uri + ' ' + alias);
		downloadEntity.extendedUris.push_back(std::move(extendedUri));
		downloadEntity.targetPath = downloadPath;
		downloadEntity.size = downloadRecordIt->size;

		auto hashSums = downloadRecordIt->hashSums;
		downloadEntity.postAction = [&hashSums, &downloadPath, &uncompressingSub]() -> string
		{
			if (!hashSums.verify(downloadPath))
			{
				if (unlink(downloadPath.c_str()) == -1)
				{
					warn("unable to remove partial index file '%s': EEE", downloadPath.c_str());
				}
				return __("hash sums mismatch");
			}
			return uncompressingSub();
		};
		auto downloadError = downloadManager.download(
				vector< download::Manager::DownloadEntity >{ downloadEntity });
		if (downloadError.empty())
		{
			return true;
		}
	}

	// we reached here if neither download URI succeeded
	warn("failed to download index for '%s/%s'",
			indexEntry.distribution.c_str(), indexEntry.component.c_str());
	return false;
}

void WorkerImpl::__update_translations(download::Manager& downloadManager,
		const Cache::IndexEntry& indexEntry, bool indexFileChanged)
{
	bool simulating = __config->getBool("cupt::worker::simulate");
	// downloading file containing localized descriptions
	auto downloadInfo = __cache->getDownloadInfoOfLocalizedDescriptions(indexEntry);
	string downloadError;
	FORIT(downloadRecordIt, downloadInfo)
	{
		const string& uri = downloadRecordIt->uri;
		const string& targetPath = downloadRecordIt->localPath;

		auto downloadPath = getDownloadPath(targetPath) + getFilenameExtension(uri);

		std::function< string () > uncompressingSub;
		if (!generateUncompressingSub(uri, downloadPath, targetPath, uncompressingSub))
		{
			continue;
		}

		auto alias = indexEntry.distribution + '/' + indexEntry.component +
				' ' + getUriBasename(uri);

		if (!simulating)
		{
			// here we check for outdated dangling indexes in partial directory
			if (indexFileChanged && fs::exists(downloadPath))
			{
				if (unlink(downloadPath.c_str()) == -1)
				{
					warn("unable to remove outdated partial index localization file '%s': EEE",
							downloadPath.c_str());
				}
			}
		}

		download::Manager::DownloadEntity downloadEntity;

		download::Manager::ExtendedUri extendedUri(download::Uri(uri),
				alias, indexEntry.uri + ' ' + alias);
		downloadEntity.extendedUris.push_back(std::move(extendedUri));
		downloadEntity.targetPath = downloadPath;
		downloadEntity.size = (size_t)-1;
		downloadEntity.postAction = uncompressingSub;

		auto downloadError = downloadManager.download(
				vector< download::Manager::DownloadEntity >{ downloadEntity });
		if (downloadError.empty())
		{
			return;
		}
	}
}

bool WorkerImpl::__update_release_and_index_data(download::Manager& downloadManager,
		const Cache::IndexEntry& indexEntry)
{
	// phase 1
	bool releaseFileChanged;
	if (!__update_release(downloadManager, indexEntry, releaseFileChanged))
	{
		return false;
	}

	// phase 2
	bool indexFileChanged;
	if (!__update_index(downloadManager, indexEntry, releaseFileChanged, indexFileChanged))
	{
		return false;
	}

	__update_translations(downloadManager, indexEntry, indexFileChanged);
	return true;
}

void WorkerImpl::updateReleaseAndIndexData(const shared_ptr< download::Progress >& downloadProgress)
{
	auto indexesDirectory = __get_indexes_directory();
	bool simulating = __config->getBool("cupt::worker::simulate");

	shared_ptr< internal::Lock > lock;
	if (!simulating)
	{
		lock.reset(new internal::Lock(__config, indexesDirectory + "/lock"));
	}

	{ // run pre-actions
		auto preCommands = __config->getList("apt::update::pre-invoke");
		FORIT(commandIt, preCommands)
		{
			__run_dpkg_command("pre", *commandIt, *commandIt);
		}
	}

	if (!simulating)
	{
		// unconditional clearing of partial chunks of Release[.gpg] files
		auto partialIndexesDirectory = indexesDirectory + __partial_directory_suffix;
		auto paths = fs::glob(partialIndexesDirectory + "/*Release*");
		FORIT(pathIt, paths)
		{
			unlink(pathIt->c_str()); // without error-checking, yeah
		}

		// also create directory if it doesn't exist
		if (! fs::exists(partialIndexesDirectory))
		{
			if (mkdir(partialIndexesDirectory.c_str(), 0755) == -1)
			{
				fatal("unable to create partial directory '%s': EEE", partialIndexesDirectory.c_str());
			}
		}
	}

	int masterExitCode = true;
	{ // download manager involved part
		download::Manager downloadManager(__config, downloadProgress);

		set< int > pids;

		auto indexEntries = __cache->getIndexEntries();
		FORIT(indexEntryIt, indexEntries)
		{
			auto pid = fork();
			if (pid == -1)
			{
				fatal("fork failed: EEE");
			}

			if (pid)
			{
				// master process
				pids.insert(pid);
			}
			else
			{
				// child process
				bool success; // bad by default

				// wrapping all errors here
				try
				{
					success = __update_release_and_index_data(downloadManager, *indexEntryIt);
				}
				catch (...)
				{
					success = false;
				}
				_exit(success ? 0 : EXIT_FAILURE);
			}
		}
		while (!pids.empty())
		{
			int status;
			pid_t pid = wait(&status);
			if (pid == -1)
			{
				fatal("wait failed: EEE");
			}
			pids.erase(pid);
			// if something went bad in child, the parent won't return non-zero code too
			if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			{
				masterExitCode = false;
			}
		}
	};

	lock.reset();

	{ // run post-actions
		auto postCommands = __config->getList("apt::update::post-invoke");
		FORIT(commandIt, postCommands)
		{
			__run_dpkg_command("post", *commandIt, *commandIt);
		}
		if (masterExitCode)
		{
			auto postSuccessCommands = __config->getList("apt::update::post-invoke-success");
			FORIT(commandIt, postSuccessCommands)
			{
				__run_dpkg_command("post", *commandIt, *commandIt);
			}
		}
	}

	if (!masterExitCode)
	{
		fatal("there were errors while downloading release and index data");
	}
}

vector< pair< string, shared_ptr< const BinaryVersion > > > WorkerImpl::getArchivesInfo() const
{
	map< string, shared_ptr< const BinaryVersion > > knownArchives;

	auto archivesDirectory = __get_archives_directory();

	auto pathMaxLength = pathconf("/", _PC_PATH_MAX);
	vector< char > pathBuffer(pathMaxLength + 1, '\0');

	auto packageNames = __cache->getBinaryPackageNames();
	FORIT(packageNameIt, packageNames)
	{
		auto package = __cache->getBinaryPackage(*packageNameIt);
		if (!package)
		{
			continue;
		}

		auto versions = package->getVersions();
		FORIT(versionIt, versions)
		{
			auto path = archivesDirectory + '/' + __get_archive_basename(*versionIt);
			if (fs::exists(path))
			{
				knownArchives[path] = *versionIt;

				// checking for symlinks
				if (readlink(path.c_str(), &pathBuffer[0], pathMaxLength) == -1)
				{
					if (errno != EINVAL)
					{
						warn("readlink on '%s' failed: EEE", path.c_str());
					}
					// not a symlink
				}
				else
				{
					// a symlink
					string targetPath(&pathBuffer[0]);
					if (fs::exists(targetPath))
					{
						knownArchives[targetPath] = *versionIt;
					}
				}
			}
		}
	}

	auto paths = fs::glob(archivesDirectory + "/*.deb");

	vector< pair< string, shared_ptr< const BinaryVersion > > > result;

	FORIT(pathIt, paths)
	{
		shared_ptr< const BinaryVersion > version; // empty by default
		auto knownPathIt = knownArchives.find(*pathIt);
		if (knownPathIt != knownArchives.end())
		{
			version = knownPathIt->second;
		}
		result.push_back(make_pair(*pathIt, version));
	}

	return result;
}

void WorkerImpl::deleteArchive(const string& path)
{
	// don't use ::realpath(), otherwise we won't delete symlinks
	auto archivesDirectory = __get_archives_directory();
	if (path.compare(0, archivesDirectory.size(), archivesDirectory))
	{
		fatal("path '%s' lies outside archives directory '%s'",
				path.c_str(), archivesDirectory.c_str());
	}

	if (!__config->getBool("cupt::worker::simulate"))
	{
		if (unlink(path.c_str()) == -1)
		{
			fatal("unable to delete file '%s'", path.c_str());
		}
	}
	else
	{
		auto filename = fs::filename(path);
		simulate("deleting an archive '%s'", filename.c_str());
	}
}

}

namespace system {

Worker::Worker(const shared_ptr< const Config >& config, const shared_ptr< const Cache >& cache)
	: __impl(new internal::WorkerImpl(config, cache))
{}

Worker::~Worker()
{
	delete __impl;
}

void Worker::setDesiredState(const Resolver::SuggestedPackages& desiredState)
{
	__impl->setDesiredState(desiredState);
}

shared_ptr< const Worker::ActionsPreview > Worker::getActionsPreview() const
{
	return __impl->getActionsPreview();
}

map< string, ssize_t > Worker::getUnpackedSizesPreview() const
{
	return __impl->getUnpackedSizesPreview();
}

pair< size_t, size_t > Worker::getDownloadSizesPreview() const
{
	return __impl->getDownloadSizesPreview();
}

void Worker::markAsAutomaticallyInstalled(const string& packageName, bool targetStatus)
{
	__impl->markAsAutomaticallyInstalled(packageName, targetStatus);
}

void Worker::changeSystem(const shared_ptr< download::Progress >& progress)
{
	__impl->changeSystem(progress);
}

void Worker::updateReleaseAndIndexData(const shared_ptr< download::Progress >& progress)
{
	__impl->updateReleaseAndIndexData(progress);
}

vector< pair< string, shared_ptr< const BinaryVersion > > > Worker::getArchivesInfo() const
{
	return __impl->getArchivesInfo();
}

void Worker::deleteArchive(const string& path)
{
	return __impl->deleteArchive(path);
}

}
}

