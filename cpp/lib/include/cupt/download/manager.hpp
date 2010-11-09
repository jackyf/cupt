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
#ifndef CUPT_DOWNLOAD_MANAGER_SEEN
#define CUPT_DOWNLOAD_MANAGER_SEEN

/// @file

#include <functional>

#include <cupt/common.hpp>
#include <cupt/fwd.hpp>
#include <cupt/download/uri.hpp>

namespace cupt {

namespace internal {

class ManagerImpl;

}

namespace download {

/// performs downloads
class Manager
{
	internal::ManagerImpl* __impl;
 public:
	/// uri with aliases
	struct ExtendedUri
	{
		Uri uri; ///< uri
		string shortAlias; ///< short alias
		string longAlias; ///< long alias (full description)

		/// trivial constructor
		/**
		 * @param uri_
		 * @param shortAlias_
		 * @param longAlias_
		 */
		ExtendedUri(const Uri& uri_, const string& shortAlias_, const string& longAlias_)
			: uri(uri_), shortAlias(shortAlias_), longAlias(longAlias_)
		{}
	};
	/// downloadable element
	struct DownloadEntity
	{
		vector< ExtendedUri > extendedUris; ///< list of alternative uris
		string targetPath; ///< path where to place downloaded file
		size_t size; ///< file size, in bytes; set @c -1 if unknown
		/// post-download callback
		/**
		 * Returned empty string means no errors.
		 * Returned non-empty string marks a download as failed and sets this
		 * string as download result.
		 */
		std::function< string () > postAction;
	};

	/// constructor
	/**
	 * @param config configuration
	 * @param progress progress meter
	 */
	Manager(const shared_ptr< const Config >& config, const shared_ptr< Progress >& progress);
	/// destructor
	~Manager();

	/// downloads entities in parallel
	/**
	 * @param entities list of entities to download
	 * @return empty string when everything went ok, human readable download
	 * error from arbitrary failed entity if some entities failed to download
	 */
	string download(const vector< DownloadEntity >& entities);
};

}
}

#endif

