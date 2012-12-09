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
using std::unique_ptr;

using boost::xpressive::sregex;
using boost::xpressive::smatch;

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
	typedef unordered_map< string, vector< PrePackageRecord > > PrePackageMap;
	struct TranslationPosition
	{
		File* file;
		size_t offset;
	};

	map< string, set< const string* > > canProvide;
	mutable unordered_map< string, unique_ptr< Package > > binaryPackages;
	mutable unordered_map< string, unique_ptr< Package > > sourcePackages;
	map< string, TranslationPosition > translations;
	mutable unordered_map< string, vector< const BinaryVersion* > > getSatisfyingVersionsCache;
	shared_ptr< PinInfo > pinInfo;
	mutable map< const Version*, ssize_t > pinCache;
	map< string, shared_ptr< ReleaseInfo > > releaseInfoCache;
	list< File > translationFileStorage;
	smatch* __smatch_ptr;

	Package* newSourcePackage() const;
	Package* newBinaryPackage() const;
	Package* preparePackage(unordered_map< string, vector< PrePackageRecord > >&,
			unordered_map< string, unique_ptr< Package > >&, const string&,
			decltype(&CacheImpl::newBinaryPackage)) const;
	shared_ptr< ReleaseInfo > getReleaseInfo(const Config&, const IndexEntry&);
	void parseSourceList(const string& path);
	void processIndexEntry(const IndexEntry&, const ReleaseLimits&);
	void processIndexFile(const string& path, IndexEntry::Type category,
			shared_ptr< const ReleaseInfo >, const string&);
	void processTranslationFiles(const IndexEntry&, const string&);
	void processTranslationFile(const string& path, const string&);
	vector< const BinaryVersion* > getSatisfyingVersionsNonCached(const Relation&) const;
	vector< const BinaryVersion* > getSatisfyingVersionsNonCached(const RelationExpression&) const;
	ssize_t computePin(const Version*, const BinaryPackage*) const;
 public:
	shared_ptr< const Config > config;
	unique_ptr< const string > binaryArchitecture;
	unique_ptr< const system::State > systemState;
	vector< IndexEntry > indexEntries;
	vector< shared_ptr< const ReleaseInfo > > sourceReleaseData;
	vector< shared_ptr< const ReleaseInfo > > binaryReleaseData;
	mutable PrePackageMap preSourcePackages;
	mutable PrePackageMap preBinaryPackages;
	list< pair< shared_ptr< const ReleaseInfo >, shared_ptr< File > > >
			releaseInfoAndFileStorage;
	ExtendedInfo extendedInfo;

	CacheImpl();
	~CacheImpl();
	void parseSourcesLists();
	void processIndexEntries(bool, bool);
	void parsePreferences();
	void parseExtendedStates();
	const BinaryPackage* getBinaryPackage(const string& packageName) const;
	const SourcePackage* getSourcePackage(const string& packageName) const;
	ssize_t getPin(const Version*, const std::function< const BinaryPackage* () >&) const;
	string getLocalizedDescription(const BinaryVersion*) const;
	void processProvides(const string*, const char*, const char*);
	vector< const BinaryVersion* > getSatisfyingVersions(const RelationExpression&) const;
};

}
}

#endif

