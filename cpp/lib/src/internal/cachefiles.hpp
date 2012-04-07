/**************************************************************************
*   Copyright (C) 2011 by Eugene V. Lyubimkin                             *
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
#ifndef CUPT_INTERNAL_CACHEFILES_SEEN
#define CUPT_INTERNAL_CACHEFILES_SEEN

#include <cupt/fwd.hpp>
#include <cupt/cache.hpp>

namespace cupt {
namespace internal {
namespace cachefiles {

typedef Cache::IndexEntry IndexEntry;
typedef Cache::IndexDownloadRecord FileDownloadRecord;

string getPathOfIndexList(const Config&, const IndexEntry&);
string getPathOfReleaseList(const Config&, const IndexEntry&);
string getPathOfExtendedStates(const Config&);

string getDownloadUriOfReleaseList(const IndexEntry&);
vector< FileDownloadRecord > getDownloadInfoOfIndexList(
		const Config&, const IndexEntry&);

vector< pair< string, string > > getPathsOfLocalizedDescriptions(
		const Config&, const IndexEntry& entry);
// TODO/API break/: deprecated, delete it
vector< Cache::LocalizationDownloadRecord > getDownloadInfoOfLocalizedDescriptions(
		const Config&, const IndexEntry&);

vector< FileDownloadRecord > getDownloadInfoOfLocalizationIndex(
		const Config&, const IndexEntry&);
struct LocalizationDownloadRecord2
{
	string filePart;
	string localPath;
};
// TODO: remove when oldstable >> wheezy
vector< LocalizationDownloadRecord2 > getDownloadInfoOfLocalizedDescriptions2(
		const Config&, const IndexEntry&);

struct LocalizationDownloadRecord3
{
	string localPath;
	string language;
	vector< FileDownloadRecord > fileDownloadRecords;
};
vector< LocalizationDownloadRecord3 > getDownloadInfoOfLocalizedDescriptions3(
		const Config&, const IndexEntry&);

bool verifySignature(const Config&, const string& path, const string& alias);
shared_ptr< cache::ReleaseInfo > getReleaseInfo(const Config&,
		const string& path, const string& alias);

}
}
}

#endif

