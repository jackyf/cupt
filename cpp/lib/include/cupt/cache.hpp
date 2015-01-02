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

#include <set>
#include <map>

#include <cupt/common.hpp>
#include <cupt/fwd.hpp>
#include <cupt/range.hpp>

namespace cupt {

namespace internal {

struct CacheImpl;

}

using std::set;
using std::map;

using namespace cache;

/// the source of package and version information
class CUPT_API Cache
{
 public:
	/// describes smallest index source piece
	/**
	 * When Cache reads source entries from configuration files, it breaks them
	 * into these logical pieces.
	 */
	struct IndexEntry
	{
		/// does this index entry contains source or binary packages
		enum Type { Source, Binary } category;
		string uri; ///< base index URI, as specified in source list
		string distribution; ///< distribution part, e.g. @c lenny, @c squeeze
		string component; ///< component part, e.g. @c main, @c contrib, @c non-free
		map< string, string > options; ///< key-value options;
	};
	/// extended package information
	struct ExtendedInfo
	{
		set< string > automaticallyInstalled; ///< names of automatically installed packages
	};

	class PackageNameIterator
	{
	 public:
		typedef const string value_type;

		class Impl;
		PackageNameIterator(Impl* impl);
		~PackageNameIterator();
		PackageNameIterator(const PackageNameIterator&);
		PackageNameIterator& operator=(const PackageNameIterator&);
		PackageNameIterator(PackageNameIterator&&) = default;

		bool operator==(const PackageNameIterator&) const;
		bool operator!=(const PackageNameIterator&) const;

		value_type& operator*() const;
		PackageNameIterator& operator++();

	 private:
		Impl* p_impl;
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
	 * @param config
	 * @param useSource whether to read source package metadata
	 * @param useBinary whether to read binary package metadata
	 * @param useInstalled whether to read dpkg metadata (installed binary packages)
	 */
	Cache(shared_ptr< const Config > config, bool useSource, bool useBinary, bool useInstalled);
	/// destructor
	virtual ~Cache();

	/// gets release data list of indexed metadata for binary packages
	vector< shared_ptr< const ReleaseInfo > > getBinaryReleaseData() const;
	/// gets release data list of indexed metadata for source packages
	vector< shared_ptr< const ReleaseInfo > > getSourceReleaseData() const;

	/// gets the list of names of available binary packages
	Range< PackageNameIterator > getBinaryPackageNames() const;
	/// gets BinaryPackage by name
	/**
	 * @param packageName name of the binary package
	 * @return pointer to binary package if found, empty pointer if not
	 */
	const BinaryPackage* getBinaryPackage(const string& packageName) const;
	/// gets the list of names of available source packages
	Range< PackageNameIterator > getSourcePackageNames() const;
	/// gets SourcePackage by name
	/**
	 * @param packageName name of the source package
	 * @return pointer to source package if found, empty pointer if not
	 */
	const SourcePackage* getSourcePackage(const string& packageName) const;

	/// gets all installed versions
	vector< const BinaryVersion* > getInstalledVersions() const;

	/// is binary package automatically installed?
	/**
	 * @param packageName name of the binary package
	 * @return @c true if yes, @c false if no
	 */
	bool isAutomaticallyInstalled(const string& packageName) const;

	/// gets list of available index entries
	vector< IndexEntry > getIndexEntries() const;

	/// gets system state
	const system::State* getSystemState() const;

	/// gets pin value for a version
	ssize_t getPin(const Version*) const;

	/// contains version and its corresponding priority
	struct VersionWithPriority
	{
		const Version* version;
		ssize_t priority;
	};
	/// gets list of versions with pins of certain package
	vector<VersionWithPriority> getSortedPinnedVersions(const Package*) const;
	/// gets version of highest pin from the package
	const Version* getPreferredVersion(const Package*) const;

	/// gets list of binary versions which satisfy given relation expression
	vector< const BinaryVersion* > getSatisfyingVersions(const RelationExpression&) const;

	/// gets extended info
	const ExtendedInfo& getExtendedInfo() const;

	/// gets localized description for the binary version
	/**
	 * @return localized description if available, version description otherwise
	 */
	string getLocalizedDescription(const BinaryVersion*) const;

	/// gets a supposed system path of package copyright file for certain binary version
	/**
	 * You must not assume that the file actually exists even if installed
	 * version is passed as parameter.
	 */
	static string getPathOfCopyright(const BinaryVersion*);
	/// gets a supposed system path of package changelog file for certain binary version
	/**
	 * You must not assume that the file actually exists even if installed
	 * version is passed as parameter.
	 */
	static string getPathOfChangelog(const BinaryVersion*);

	/// controls internal caching
	/**
	 * If set to @c true, enables internal caching in methods @ref getPin and
	 * @ref getSatisfyingVersions. Defaults to @c false.
	 */
	static bool memoize;
};

}

#endif

