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
#ifndef CUPT_SYSTEM_SNAPSHOTS
#define CUPT_SYSTEM_SNAPSHOTS

/// @file

#include <cupt/common.hpp>
#include <cupt/fwd.hpp>

namespace cupt {

namespace internal {

class SnapshotsImpl;

}

namespace system {

/// various snapshot-related routines
class Snapshots
{
	internal::SnapshotsImpl* __impl;

	Snapshots(const Snapshots&);
	Snapshots& operator=(const Snapshots&);
 public:
	/// @cond
	static const string installedPackageNamesFilename;
	/// @endcond

	/// constructor
	/**
	 * @param config configuration
	 */
	Snapshots(const shared_ptr< Config >& config);
	/// destructor
	~Snapshots();
	/// returns array of names of available snapshots
	vector< string > getSnapshotNames() const;
	/**
	 * @return full path to directory containing snapshots
	 */
	string getSnapshotsDirectory() const;
	/**
	 * @param snapshotName
	 * @return full path to directory containing snapshot with the name @a snapshotName
	 */
	string getSnapshotDirectory(const string& snapshotName) const;
	/**
	 * Modifies config (passed in constructor) in the way that Cache built from
	 * it have access only to installed and snapshot versions of packages.
	 *
	 * @param snapshotName
	 */
	void setupConfigForSnapshotOnly(const string& snapshotName);
	/**
	 * Schedules snapshot versions of packages to be installed.
	 *
	 * @param snapshotName
	 * @param cache
	 * @param resolver
	 */
	void setupResolverForSnapshotOnly(const string& snapshotName,
			const Cache& cache, Resolver& resolver);
};

}
}

#endif

