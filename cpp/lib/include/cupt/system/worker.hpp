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

}
}

#endif

