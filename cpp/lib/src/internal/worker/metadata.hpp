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
#include <internal/cachefiles.hpp>

namespace cupt {
namespace internal {

class MetadataWorker: public virtual WorkerBase
{
	enum class IndexType;
	struct IndexUpdateInfo;

	static bool __is_diff_type(const IndexType&);
	string __get_indexes_directory() const;

	typedef std::function< string () > (*SecondPostActionGeneratorForReleaseLike)(const Config&, const string&, const string&);
	bool __downloadReleaseLikeFile(download::Manager&,
			const string&, const string&,
			const cachefiles::IndexEntry&, const string&,
			SecondPostActionGeneratorForReleaseLike);
	bool __update_release_and_index_data(download::Manager&, const cachefiles::IndexEntry&);
	bool __update_release(download::Manager&, const cachefiles::IndexEntry&, bool& releaseFileChanged);
	bool __downloadRelease(download::Manager&, const cachefiles::IndexEntry&, bool& releaseFileChanged);
	bool __downloadInRelease(download::Manager&, const cachefiles::IndexEntry&, bool& releaseFileChanged);
	ssize_t __get_uri_priority(const string& uri);
	bool __download_index(download::Manager&, const cachefiles::FileDownloadRecord&, IndexType,
			const cachefiles::IndexEntry&, const string&, const string&, bool);
	bool __update_index(download::Manager&, const cachefiles::IndexEntry&,
			IndexUpdateInfo&&, bool, bool&);
	void p_generateIndexesOfIndexes(const cachefiles::IndexEntry&);
	bool __update_main_index(download::Manager&, const cachefiles::IndexEntry&,
			bool releaseFileChanged, bool& indexFileChanged);
	void __update_translations(download::Manager& downloadManager,
			const cachefiles::IndexEntry&, bool indexFileChanged);
	void __list_cleanup(const string&);
	bool p_runMetadataUpdateThreads(const shared_ptr< download::Progress >&);
	bool p_metadataUpdateThread(download::Manager&, const cachefiles::IndexEntry&);
 public:
	void updateReleaseAndIndexData(const shared_ptr< download::Progress >&);
};

}
}

#endif

