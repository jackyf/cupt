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
#ifndef CUPT_DOWNLOAD_URI_SEEN
#define CUPT_DOWNLOAD_URI_SEEN

/// @file

#include <cupt/common.hpp>

namespace cupt {

namespace internal {

struct UriData;

}

namespace download {

/// uniform resource indentifier, "download path"
class Uri
{
	internal::UriData* __data;
 public:
	/// constructor
	/**
	 * @param uri string representation of URI
	 */
	Uri(const string& uri);
	/// copy constructor
	/**
	 * @param other object to copy from
	 */
	Uri(const Uri& other);
	Uri& operator=(const Uri&);
	virtual ~Uri();

	string getProtocol() const;
	string getHost() const;
	string getOpaque() const;
	operator string() const;
};

}
}

#endif

