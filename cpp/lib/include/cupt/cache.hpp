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
#ifndef CUPT_CACHE_CACHE_SEEN
#define CUPT_CACHE_CACHE_SEEN

/// @file

#include <boost/xpressive/xpressive_fwd.hpp>

#include <set>

#include <cupt/common.hpp>
#include <cupt/fwd.hpp>
#include <cupt/hashsums.hpp>

namespace cupt {

namespace internal {

struct CacheImpl;

}

using std::set;

using namespace cache;

/// the source of package and version information
class Cache
{
 public:
	/// describes smallest index source piece
	/**
	 * When Cache reads source entries from configuration files, it breaks them
	 * into these logical pieces.
	 */
	struct IndexEntry
	{
		enum Type { Source, Binary } category; ///< does this index entry contains source or binary packages
		string uri; ///< base index URI, as specified in source list
		string distribution; ///< distribution part, e.g. @c lenny, @c squeeze
		string component; ///< component part, e.g. @c main, @c contrib, @c non-free
	};
	/// download record for cache index files
	struct IndexDownloadRecord
	{
		string uri; ///< download URI
		uint32_t size; ///< size in bytes
		HashSums hashSums; ///< hash sums
	};
	/// download record for localization files
	struct LocalizationDownloadRecord
	{
		string uri; ///< download URI
		string localPath; ///< path, where download to
	};
	/// extended package information
	struct ExtendedInfo
	{
		set< string > automaticallyInstalled; ///< names of automatically installed packages
	};

 private:
	internal::CacheImpl* __impl;
	Cache(const Cache&);
	Cache& operator=(const Cache&);
 public:
	/// constructor
	/**
	 * Reads package metadata and builds index on it.
	 *
	 * @param config configuration
	 * @param useSource whether to read source package metadata
	 * @param useBinary whether to read binary package metadata
	 * @param useInstalled whether to read dpkg metadata (installed binary packages)
	 * @param packageNameGlobsToReinstall array of glob expressions, allow these packages to be re-installed
	 */
	Cache(shared_ptr< const Config > config, bool useSource, bool useBinary, bool useInstalled,
			const vector< string >& packageNameGlobsToReinstall = vector< string >());
	/// destructor
	virtual ~Cache();

	/// gets release data list of indexed metadata for binary packages
	vector< shared_ptr< const ReleaseInfo > > getBinaryReleaseData() const;
	/// gets release data list of indexed metadata for source packages
	vector< shared_ptr< const ReleaseInfo > > getSourceReleaseData() const;

	/// gets the list of names of available binary packages
	vector< string > getBinaryPackageNames() const;
	/// gets BinaryPackage by name
	/**
	 * @param packageName name of the binary package
	 */
	shared_ptr< const BinaryPackage > getBinaryPackage(const string& packageName) const;
	/// gets the list of names of available source packages
	vector< string > getSourcePackageNames() const;
	/// gets SourcePackage by name
	/**
	 * @param packageName name of the source package
	 */
	shared_ptr< const SourcePackage > getSourcePackage(const string& packageName) const;

	/// gets all installed versions
	vector< shared_ptr< const BinaryVersion > > getInstalledVersions() const;

	bool isAutomaticallyInstalled(const string& packageName) const;

	vector< IndexEntry > getIndexEntries() const;

	string getPathOfReleaseList(const IndexEntry& entry) const;
	string getPathOfIndexList(const IndexEntry& entry) const;
	string getPathOfExtendedStates() const;

	string getDownloadUriOfReleaseList(const IndexEntry& entry) const;
	vector< IndexDownloadRecord > getDownloadInfoOfIndexList(const IndexEntry&) const;
	vector< LocalizationDownloadRecord > getDownloadInfoOfLocalizedDescriptions(const IndexEntry&) const;

	shared_ptr< const system::State > getSystemState() const;

	ssize_t getPin(const shared_ptr< const Version >&) const;

	struct PinnedVersion
	{
		shared_ptr< const Version > version;
		ssize_t pin;

		PinnedVersion(shared_ptr< const Version > _version, ssize_t _pin)
			: version(_version), pin(_pin) {}
	};
	vector< PinnedVersion > getSortedPinnedVersions(const shared_ptr< const Package >&) const;
	shared_ptr< const Version > getPolicyVersion(const shared_ptr< const Package >&) const;

	vector< shared_ptr< const BinaryVersion > > getSatisfyingVersions(const RelationExpression&) const;

	const ExtendedInfo& getExtendedInfo() const;

	pair< string, string > getLocalizedDescriptions(const shared_ptr< const BinaryVersion >&) const;

	static bool verifySignature(const shared_ptr< const Config >&, const string& path);
	static string getPathOfCopyright(const shared_ptr< const BinaryVersion >&);
	static string getPathOfChangelog(const shared_ptr< const BinaryVersion >&);

	static bool memoize;
};

}

#endif

