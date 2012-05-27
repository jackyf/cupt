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
#ifndef CUPT_SYSTEM_WORKER_SEEN
#define CUPT_SYSTEM_WORKER_SEEN

/// @file

#include <map>

#include <cupt/common.hpp>
#include <cupt/fwd.hpp>
#include <cupt/system/resolver.hpp>

namespace cupt {

namespace internal {

class WorkerImpl;

}

namespace system {

/// performs system modifications
class CUPT_API Worker
{
	internal::WorkerImpl* __impl;

	Worker(const Worker&);
	Worker& operator=(const Worker&);
 public:
	/// action types
	struct Action
	{
		enum Type {
			Install, ///< a new package is installed
			Remove, ///< the existing package is removed
			Purge, ///< the existing package is purged
			Upgrade, ///< new version of the existing package is installed
			Downgrade, ///< old version of the existing package is installed
			Configure, ///< the existing package in intermediate state is configured (properly installed)
			Deconfigure, ///< the existing package in intermediate state is removed
			ProcessTriggers, ///< triggers are processed for the existing package
			Reinstall, ///< remove and install the installed version
			Count ///< element count
		};
		static const char* rawStrings[Count]; ///< @copydoc BinaryVersion::RelationTypes::rawStrings
	};
	struct ActionsPreview
	{
		Resolver::SuggestedPackages groups[Action::Count]; ///< system changes divided by type
		/// maps package name to target 'automatically installed' flag value
		/**
		 * If a package name is not present in the map, the flag remains unchanged.
		 *
		 * If a package name is mapped to @c true, package will be marked as automatically installed.
		 *
		 * If a package name is mapped to @c false, package will be marked as manually installed.
		 */
		std::map< string, bool > autoFlagChanges;
	};

	/// constructor
	/**
	 * @param config
	 * @param cache
	 */
	Worker(const shared_ptr< const Config >& config, const shared_ptr< const Cache >& cache);
	virtual ~Worker();
	/**
	 * Sets the desired system state.
	 *
	 * May be called several times for examining different possible system states.
	 *
	 * @param offer
	 */
	void setDesiredState(const Resolver::Offer& offer);
	/**
	 * Sets the purge flag for removed packages.
	 *
	 * Removed packages can be either simply removed or removed along with
	 * their configuration files (purged).
	 *
	 * Changes which are made by this method are not visible until you call
	 * @ref setDesiredState. If some calls of this method were made after a
	 * last call to @ref setDesiredState, you must call @ref setDesiredState
	 * again.
	 *
	 * @param packageName binary package name to modify a flag value for
	 * @param value the target state of the flag
	 */
	void setPackagePurgeFlag(const string& packageName, bool value);

	/**
	 * Shouldn't be called before @ref setDesiredState.
	 *
	 * @return a set of actions to get the desired system state divided by action types
	 */
	shared_ptr< const ActionsPreview > getActionsPreview() const;
	/**
	 * Shouldn't be called before @ref setDesiredState.
	 *
	 * @return map: package name -> unpacked size change (in bytes)
	 */
	map< string, ssize_t > getUnpackedSizesPreview() const;
	/**
	 * Shouldn't be called before @ref setDesiredState.
	 *
	 * @return pair: total amount of needed binary archives (in bytes), amount to download (in bytes)
	 */
	pair< size_t, size_t > getDownloadSizesPreview() const;

	/**
	 * Marks a package as automatically or manually installed.
	 *
	 * @param packageName
	 * @param value if @c true, marks as automatically installed, if @c false, marks as manually installed
	 */
	void setAutomaticallyInstalledFlag(const string& packageName, bool value);
	/**
	 * Modifies the system to achieve the desired state set by
	 * @ref setDesiredState.
	 *
	 * @param progress
	 */
	void changeSystem(const shared_ptr< download::Progress >& progress);

	/**
	 * Downloads latest Release and Packages/Sources files from repository
	 * sources.
	 *
	 * @param progress
	 */
	void updateReleaseAndIndexData(const shared_ptr< download::Progress >& progress);

	/// gets available archives of binary versions
	/**
	 * Gets paths of all '.deb' archives in the archives directory and matches
	 * them to available binary versions. Not matched paths with be paired with
	 * an empty pointer.
	 *
	 * @return array of pairs < package name, pointer to binary version >
	 */
	vector< pair< string, const BinaryVersion* > > getArchivesInfo() const;
	/**
	 * Deletes an archive file (it may be a symlink). Verifies that deleted file is
	 * located under archives path directory.
	 *
	 * @param path absolute (i.e., not relative) path to file
	 */
	void deleteArchive(const string& path);
	/**
	 * Deletes all partially downloaded archive files.
	 */
	void deletePartialArchives();

	/**
	 * Makes a system snapshot with a name @a name.
	 *
	 * @param name the snapshot name.
	 */
	void saveSnapshot(const Snapshots&, const string& name);
	/**
	 * Renames a system snapshot.
	 *
	 * @param previousName previous snasphot name
	 * @param newName new snapshot name
	 */
	void renameSnapshot(const Snapshots&,
		const string& previousName, const string& newName);
	/**
	 * Removes a system snapshot.
	 *
	 * @param name name of the snapshot
	 */
	void removeSnapshot(const Snapshots&, const string& name);
};

}
}

#endif

