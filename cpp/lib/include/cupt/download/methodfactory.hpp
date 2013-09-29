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
#ifndef CUPT_DOWNLOAD_METHODFACTORY_SEEN
#define CUPT_DOWNLOAD_METHODFACTORY_SEEN

/// @file

#include <cupt/fwd.hpp>
#include <cupt/common.hpp>

namespace cupt {

namespace internal {

class MethodFactoryImpl;

}

namespace download {

/// class to get a download method for URIs
class CUPT_API MethodFactory
{
	internal::MethodFactoryImpl* __impl;
	MethodFactory(const MethodFactory&);
	MethodFactory& operator=(const MethodFactory);
 public:
	/// constructor
	/**
	 * @param config
	 */
	MethodFactory(const Config& config);
	/// destructor
	~MethodFactory();
	/// gets download method for @a uri
	/**
	 * @param uri
	 */
	Method* getDownloadMethodForUri(const Uri& uri) const;
};

}
}

#endif

