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
#ifndef CUPT_INTERNAL_CACHEBASE_SEEN
#define CUPT_INTERNAL_CACHEBASE_SEEN

#include <unordered_map>
#include <list>

#include <boost/xpressive/xpressive_fwd.hpp>

#include <cupt/common.hpp>
#include <cupt/fwd.hpp>
#include <cupt/cache.hpp>

namespace cupt {
namespace internal {

class PinInfo;
class ReleaseLimits;

using std::list;
using std::unordered_map;

using boost::xpressive::sregex;

// this struct is solely for system::State
class CacheImpl
{
 public:
	struct PrePackageRecord
	{
		size_t offset;
		const pair< shared_ptr< const ReleaseInfo >, shared_ptr< File > >* releaseInfoAndFile;
	};
 private:
	typedef Cache::IndexEntry IndexEntry;
	typedef Cache::ExtendedInfo ExtendedInfo;
	struct TranslationPosition
	{
		shared_ptr< File > file;
		size_t offset;
	};

	map< string, set< const string* > > canProvide;
	mutable unordered_map< string, shared_ptr< Package > > binaryPackages;
	mutable unordered_map< string, shared_ptr< Package > > sourcePackages;
	map< string, TranslationPosition > translations;
	mutable unordered_map< string, vector< shared_ptr< const BinaryVersion > > > getSatisfyingVersionsCache;
	shared_ptr< PinInfo > pinInfo;
	mutable map< shared_ptr< const Version >, ssize_t > pinCache;
	map< string, shared_ptr< ReleaseInfo > > releaseInfoCache;

	shared_ptr< Package > newSourcePackage(const string&) const;
	shared_ptr< Package > newBinaryPackage(const string&) const;
	shared_ptr< Package > preparePackage(unordered_map< string, vector< PrePackageRecord > >&,
			unordered_map< string, shared_ptr< Package > >&, const string&,
			decltype(&CacheImpl::newBinaryPackage)) const;
	shared_ptr< ReleaseInfo > getReleaseInfo(const Config&, const IndexEntry&);
	void parseSourceList(const string& path);
	void processIndexEntry(const IndexEntry&, const ReleaseLimits&);
	void processIndexFile(const string& path, IndexEntry::Type category,
			shared_ptr< const ReleaseInfo >);
	void processTranslationFile(const string& path);
	vector< shared_ptr< const BinaryVersion > > getSatisfyingVersions(const Relation&) const;
 public:
	shared_ptr< const Config > config;
	shared_ptr< const string > binaryArchitecture;
	vector< shared_ptr< sregex > > packageNameRegexesToReinstall;
	shared_ptr< const system::State > systemState;
	vector< IndexEntry > indexEntries;
	vector< shared_ptr< const ReleaseInfo > > sourceReleaseData;
	vector< shared_ptr< const ReleaseInfo > > binaryReleaseData;
	mutable unordered_map< string, vector< PrePackageRecord > > preSourcePackages;
	mutable unordered_map< string, vector< PrePackageRecord > > preBinaryPackages;
	list< pair< shared_ptr< const ReleaseInfo >, shared_ptr< File > > >
			releaseInfoAndFileStorage;
	ExtendedInfo extendedInfo;

	void parseSourcesLists();
	void processIndexEntries(bool, bool);
	void parsePreferences();
	void parseExtendedStates();
	shared_ptr< const BinaryPackage > getBinaryPackage(const string& packageName) const;
	shared_ptr< const SourcePackage > getSourcePackage(const string& packageName) const;
	ssize_t getPin(const shared_ptr< const Version >&, const std::function< string () >&) const;
	pair< string, string > getLocalizedDescriptions(const shared_ptr< const BinaryVersion >&) const;
	void processProvides(const string*, const char*, const char*);
	vector< shared_ptr< const BinaryVersion > > getSatisfyingVersions(const RelationExpression&) const;
};

}
}

#endif

