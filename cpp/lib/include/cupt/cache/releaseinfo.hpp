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
#ifndef CUPT_CACHE_RELEASE_INFO_SEEN
#define CUPT_CACHE_RELEASE_INFO_SEEN

/// @file

#include <cupt/common.hpp>

namespace cupt {
/** @namespace cupt::cache */
namespace cache {

/// release information
struct CUPT_API ReleaseInfo
{
	bool verified; ///< @c true, if information is signed and a signature is verified
	string version; ///< distribution version, may be empty
	string description; ///< distribution description, may be empty
	string vendor; ///< vendor name, may be empty
	string label; ///< human-readable label, may be empty
	string archive; ///< archive name, may be empty
	string codename; ///< release code name, may be empty
	string component; ///< component name, may be empty
	string date; ///< creation date, may be empty
	string validUntilDate; ///< date of Release file expiry, may be empty
	vector< string> architectures; ///< list of architectures applicable
	string baseUri; ///< source base URI
	bool notAutomatic; ///< @c true, if @c NotAutomatic flag is specified in Release file
	bool butAutomaticUpgrades; ///< @c true, if @c ButAutomaticUpgrades flag is specified in Release file
};

}
}

#endif

