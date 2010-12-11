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
#ifndef CUPT_INTERNAL_WORKER_SNAPSHOTS_SEEN
#define CUPT_INTERNAL_WORKER_SNAPSHOTS_SEEN

#include <cupt/fwd.hpp>

#include <internal/worker/base.hpp>

namespace cupt {
namespace internal {

using system::Snapshots;

class SnapshotsWorker: public virtual WorkerBase
{
	void __delete_temporary(const string&, bool);
	void __do_repacks(const vector< string >&, bool);
	string __create_index_file(const Cache::IndexEntry&);
	void __create_release_file(const string&, const string&,
			const string&, const Cache::IndexEntry&, bool);
 public:
	void saveSnapshot(const Snapshots&, const string& name);
	void renameSnapshot(const Snapshots& snapshots,
		const string& previousName, const string& newName);
	void removeSnapshot(const Snapshots& snapshots, const string& name);
};

}
}

#endif

