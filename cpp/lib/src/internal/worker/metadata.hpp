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
#ifndef CUPT_INTERNAL_WORKER_METADATA_SEEN
#define CUPT_INTERNAL_WORKER_METADATA_SEEN

#include <cupt/cache.hpp>

#include <internal/worker/base.hpp>

namespace cupt {
namespace internal {

class MetadataWorker: public virtual WorkerBase
{
	string __get_indexes_directory() const;
	bool __update_release_and_index_data(download::Manager&, const Cache::IndexEntry&);
	bool __update_release(download::Manager&, const Cache::IndexEntry&, bool& releaseFileChanged);
	ssize_t __get_uri_priority(const string& uri);
	bool __download_index(download::Manager&, const Cache::IndexDownloadRecord&, bool,
			const Cache::IndexEntry&, const string&, const string&, bool, bool);
	bool __update_index(download::Manager&, const Cache::IndexEntry&,
			bool releaseFileChanged, bool& indexFileChanged);
	void __update_translations(download::Manager& downloadManager,
			const Cache::IndexEntry&, bool indexFileChanged);
	void __list_cleanup(const string&);
 public:
	void updateReleaseAndIndexData(const shared_ptr< download::Progress >&);
};

}
}

#endif

