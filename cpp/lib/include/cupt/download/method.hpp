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
#ifndef CUPT_DOWNLOAD_METHOD_SEEN
#define CUPT_DOWNLOAD_METHOD_SEEN

/// @file

#include <functional>

#include <cupt/common.hpp>
#include <cupt/fwd.hpp>

namespace cupt {
namespace download {

/// base class of download methods
class Method
{
 protected:
	Method();
	/// gets URI-specific value of some 'acquire::*' option
	/**
	 * Options of 'Acquire' group can be overridden for specific host.  This
	 * function hides the details and provides the convenient way get the value
	 * of the option in 'Acquire' group for certain URI.
	 *
	 * @param config configuration
	 * @param uri uri
	 * @param suboptionName suboption name
	 *
	 * @par Example:
	 * @code
	 * auto proxy = getAcquireSuboptionForUri(config, uri, "proxy");
	 * @endcode
	 */
	static string getAcquireSuboptionForUri(const shared_ptr< const Config >& config,
			const Uri& uri, const string& suboptionName);
	/// gets URI-specific value of some integer 'acquire::*' option
	/**
	 * Same as @ref getAcquireSuboptionForUri, but for integer options.
	 *
	 * @param config configuration
	 * @param uri uri
	 * @param suboptionName suboption name
	 */
	static ssize_t getIntegerAcquireSuboptionForUri(const shared_ptr< const Config >&,
			const Uri& uri, const string& suboptionName);
 public:
	virtual string perform(const shared_ptr< const Config >&, const Uri& uri,
			const string& targetPath, const std::function< void (const vector< string >&) >& callback) = 0;
};

}
}

#endif

