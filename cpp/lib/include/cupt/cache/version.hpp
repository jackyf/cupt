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
#ifndef CUPT_CACHE_VERSION_SEEN
#define CUPT_CACHE_VERSION_SEEN

/// @file

#include <cstdint>
#include <map>

#include <cupt/fwd.hpp>
#include <cupt/common.hpp>
#include <cupt/hashsums.hpp>

namespace cupt {
namespace cache {

using std::map;

/// common version information
/**
 * @see SourceVersion and BinaryVersion
 */
struct Version
{
	/// where version comes from
	struct Source
	{
		shared_ptr< const ReleaseInfo > release; ///< release info
		string directory; ///< remote directory containing files
	};
	/// standard initialization parameters
	struct InitializationParameters
	{
		string packageName; ///< package name
		shared_ptr< File > file; ///< file to read from
		uint32_t offset; ///< version record offset in @ref file
		shared_ptr< const ReleaseInfo > releaseInfo; ///< release info
	};
	/// download place record
	struct DownloadRecord
	{
		string baseUri; ///< base URI
		string directory; ///< directory on the @ref baseUri
	};
	/// priority
	struct Priorities
	{
		/// priority types
		enum Type { Required, Important, Standard, Optional, Extra };
		/// string values of corresponding priority types
		static const string strings[];
	};
	/// file information
	struct FileRecord
	{
		string name; ///< file name
		uint32_t size; ///< file size
		HashSums hashSums; ///< hash sums
	};
	vector< Source > sources;
	string packageName;
	Priorities::Type priority;
	string section;
	string maintainer;
	string versionString;
	map< string, string >* others;

	Version();
	virtual ~Version();
	virtual bool areHashesEqual(const shared_ptr< const Version >& other) const = 0;

	bool isVerified() const;
	vector< DownloadRecord > getDownloadInfo() const;

	bool operator<(const Version& other) const;
	bool operator==(const Version& other) const;

	static bool parseRelations;
	static bool parseInfoOnly;
	static bool parseOthers;
};

} // namespace
} // namespace

#endif

