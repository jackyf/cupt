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

#include <cupt/common.hpp>
#include <cupt/fwd.hpp>
#include <cupt/system/resolver.hpp>

namespace cupt {

namespace internal {

class WorkerImpl;

}

namespace system {

/// performs system modifications
class Worker
{
	internal::WorkerImpl* __impl;

	Worker(const Worker&);
	Worker& operator=(const Worker&);
 public:
	/**
	 * Vector of Action::Count elements.
	 */
	typedef vector< Resolver::SuggestedPackages > ActionsPreview;
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
			Markauto, ///< the package is marked as automatically installed
			Unmarkauto, ///< the package is marked as manually installed
			Count ///< element count
		};
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
	 * @param desiredState
	 */
	void setDesiredState(const Resolver::SuggestedPackages& desiredState);

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
	vector< pair< string, shared_ptr< const BinaryVersion > > > getArchivesInfo() const;
	void deleteArchive(const string& path);
};

}
}

#endif

