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

#include <cstdint>
#include <map>

#include <cupt/fwd.hpp>
#include <cupt/common.hpp>
#include <cupt/hashsums.hpp>

namespace cupt {
namespace cache {

using std::map;

struct Version
{
	struct AvailableAsEntry
	{
		shared_ptr< const ReleaseInfo > release;
		string directory;
	};
	struct InitializationParameters
	{
		string packageName;
		shared_ptr< File > file;
		uint32_t offset;
		shared_ptr< const ReleaseInfo > releaseInfo;
	};
	struct DownloadRecord
	{
		string baseUri;
		string directory;
	};
	struct Priorities
	{
		enum Type { Required, Important, Standard, Optional, Extra };
		static const string strings[];
	};
	struct FileRecord
	{
		string name;
		uint32_t size;
		HashSums hashSums;
	};
	vector< AvailableAsEntry > availableAs;
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

