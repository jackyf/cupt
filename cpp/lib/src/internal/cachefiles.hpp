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
#include <cupt/hashsums.hpp>

namespace cupt {
namespace internal {
namespace cachefiles {

typedef Cache::IndexEntry IndexEntry;
struct FileDownloadRecord
{
	string uri;
	uint32_t size;
	HashSums hashSums;
};

string getPathOfIndexList(const Config&, const IndexEntry&);
string getPathOfReleaseList(const Config&, const IndexEntry&);
string getPathOfInReleaseList(const Config&, const IndexEntry&);
string getPathOfMasterReleaseLikeList(const Config&, const IndexEntry&);
string getPathOfExtendedStates(const Config&);

string getDownloadUriOfReleaseList(const IndexEntry&);
string getDownloadUriOfInReleaseList(const IndexEntry&);
vector< FileDownloadRecord > getDownloadInfoOfIndexList(
		const Config&, const IndexEntry&);

vector< pair< string, string > > getPathsOfLocalizedDescriptions(
		const Config&, const IndexEntry& entry);

struct LocalizationDownloadRecord3
{
	string localPath;
	string language;
	vector< FileDownloadRecord > fileDownloadRecords;
};
vector< LocalizationDownloadRecord3 > getDownloadInfoOfLocalizedDescriptions3(
		const Config&, const IndexEntry&);

shared_ptr< cache::ReleaseInfo > getReleaseInfo(const string& path, const string& alias);
void verifyReleaseValidityDate(const string& date, const Config& config, const string& alias);
bool verifySignature(const Config&, const string& path, const string& alias);

}
}
}

#endif

