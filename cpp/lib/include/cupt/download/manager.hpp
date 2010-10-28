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
		 * @param uri_ uri
		 * @param shortAlias_ short alias
		 * @param longAlias_ long alias
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

	Manager(const shared_ptr< const Config >&, const shared_ptr< Progress >&);
	~Manager();

	string download(const vector< DownloadEntity >&);
};

}
}

#endif

