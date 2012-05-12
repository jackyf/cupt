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
struct CUPT_API Version
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
	/// %file information
	struct FileRecord
	{
		string name; ///< file name
		uint32_t size; ///< file size
		HashSums hashSums; ///< hash sums
	};
	vector< Source > sources; ///< list of sources
	string packageName; ///< package name
	Priorities::Type priority; ///< priority
	string section; ///< section
	string maintainer; ///< maintainer (usually name and mail address)
	string versionString; ///< version
	map< string, string >* others; ///< unknown fields in the form 'name' -> 'value', @c NULL by default

	/// constructor
	Version();
	/// destructor
	virtual ~Version();
	/// determines file equality between two versions
	/**
	 * @param other version to compare with
	 * @return @c true if hash sums of all files in the version match hash sums
	 * of all files in the @a other version, @c false otherwise
	 */
	virtual bool areHashesEqual(const Version* other) const = 0;

	/// does version have at least one verified Source?
	bool isVerified() const;
	/// gets list of available download records for version
	vector< DownloadRecord > getDownloadInfo() const;

	/// less-than operator
	/**
	 * Uses pair @ref packageName, @ref versionString for comparison
	 */
	bool operator<(const Version&) const;
	/// equality operator
	/**
	 * Uses pair @ref packageName, @ref versionString for comparison
	 */
	bool operator==(const Version&) const;

	/// enables parsing relation fields in versions, @c true by default
	static bool parseRelations;
	/// enables parsing info-only fields in versions, @c true by default
	static bool parseInfoOnly;
	/// enables parsing unknown fields in versions, @c false by default
	static bool parseOthers;
};

} // namespace
} // namespace

#endif

