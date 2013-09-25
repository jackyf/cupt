/**************************************************************************
*   Copyright (C) 2010-2013 by Eugene V. Lyubimkin                        *
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

#include <algorithm>

#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/binaryversion.hpp>
#include <cupt/system/state.hpp>

#include <internal/cacheimpl.hpp>
#include <internal/filesystem.hpp>

namespace cupt {

typedef internal::CacheImpl::PrePackageMap PrePackageMap;
typedef internal::CacheImpl::PrePackageRecord PrePackageRecord;

struct Cache::PackageNameIterator::Impl: public PrePackageMap::const_iterator
{
	Impl(PrePackageMap::const_iterator it)
		: PrePackageMap::const_iterator(it)
	{}
};

Cache::PackageNameIterator& Cache::PackageNameIterator::operator++()
{
	++*p_impl;
	return *this;
}

Cache::PackageNameIterator::value_type& Cache::PackageNameIterator::operator*() const
{
	return (*p_impl)->first;
}

bool Cache::PackageNameIterator::operator==(const PackageNameIterator& other) const
{
	return *p_impl == *(other.p_impl);
}

bool Cache::PackageNameIterator::operator!=(const PackageNameIterator& other) const
{
	return !(*this == other);
}

Cache::PackageNameIterator::PackageNameIterator(Impl* impl)
	: p_impl(impl)
{}

Cache::PackageNameIterator::PackageNameIterator(const PackageNameIterator& other)
	: p_impl(new Impl(*other.p_impl))
{}

Cache::PackageNameIterator& Cache::PackageNameIterator::operator=(const PackageNameIterator& other)
{
	if (this != &other)
	{
		delete p_impl;
		p_impl = new Impl(*other.p_impl);
	}
	return *this;
}

Cache::PackageNameIterator::~PackageNameIterator()
{
	delete p_impl;
}

Cache::Cache(shared_ptr< const Config > config, bool useSource, bool useBinary, bool useInstalled)
{
	__impl = new internal::CacheImpl;
	__impl->config = config;
	__impl->binaryArchitecture.reset(new string(config->getString("apt::architecture")));

	{ // ugly hack to copy trusted keyring from APT whenever possible, see #647001
		auto cuptKeyringPath = config->getString("gpgv::trustedkeyring");
		auto tempPath = cuptKeyringPath + ".new.temp";

		auto result = std::system(format2("rm -f %s &&"
				"(apt-key exportall | gpg --batch --no-default-keyring --keyring %s --import) >/dev/null 2>/dev/null &&"
				"chmod -f +r %s",
				tempPath, tempPath, tempPath).c_str());
		if (result == 0)
		{
			internal::fs::move(tempPath, cuptKeyringPath); // ignoring errors
		}
		unlink(tempPath.c_str()); // in case of system() or move() above failed
	}

	__impl->parseSourcesLists();

	if (useInstalled)
	{
		__impl->systemState.reset(new system::State(config, __impl));
	}

	__impl->processIndexEntries(useBinary, useSource);
	__impl->parsePreferences();
	__impl->parseExtendedStates();
}

Cache::~Cache()
{
	delete __impl;
}

vector< shared_ptr< const ReleaseInfo > > Cache::getBinaryReleaseData() const
{
	return __impl->binaryReleaseData;
}

vector< shared_ptr< const ReleaseInfo > > Cache::getSourceReleaseData() const
{
	return __impl->sourceReleaseData;
}

vector< Cache::IndexEntry > Cache::getIndexEntries() const
{
	return __impl->indexEntries;
}

static Range< Cache::PackageNameIterator > getPrePackagesRange(const PrePackageMap& ppm)
{
	typedef Cache::PackageNameIterator PNI;
	return { PNI(new PNI::Impl(ppm.cbegin())), PNI(new PNI::Impl(ppm.cend())) };
}

Range< Cache::PackageNameIterator > Cache::getBinaryPackageNames() const
{
	return getPrePackagesRange(__impl->preBinaryPackages);
}

Range< Cache::PackageNameIterator > Cache::getSourcePackageNames() const
{
	return getPrePackagesRange(__impl->preSourcePackages);
}

const BinaryPackage* Cache::getBinaryPackage(const string& packageName) const
{
	return __impl->getBinaryPackage(packageName);
}

const SourcePackage* Cache::getSourcePackage(const string& packageName) const
{
	return __impl->getSourcePackage(packageName);
}

ssize_t Cache::getPin(const Version* version) const
{
	auto getBinaryPackageFromVersion = [this, &version]() -> const BinaryPackage*
	{
		if (dynamic_cast< const BinaryVersion* >(version))
		{
			return getBinaryPackage(version->packageName);
		}
		else
		{
			return nullptr;
		}
	};

	return __impl->getPin(version, getBinaryPackageFromVersion);
}

vector< Cache::PinnedVersion > Cache::getSortedPinnedVersions(const Package* package) const
{
	vector< Cache::PinnedVersion > result;

	auto getBinaryPackage = [&package]()
	{
		return dynamic_cast< const BinaryPackage* >(package);
	};
	for (const auto& version: *package)
	{
		result.push_back(PinnedVersion { version, __impl->getPin(version, getBinaryPackage) });
	}

	auto sorter = [](const PinnedVersion& left, const PinnedVersion& right) -> bool
	{
		if (left.pin < right.pin)
		{
			return false;
		}
		else if (left.pin > right.pin)
		{
			return true;
		}
		else
		{
			return compareVersionStrings(left.version->versionString, right.version->versionString) > 0;
		}
	};
	std::stable_sort(result.begin(), result.end(), sorter);

	return result;
}

const Version* Cache::getPreferredVersion(const Package* package) const
{
	auto sortedPinnedVersions = getSortedPinnedVersions(package);
	// not assuming the package have at least valid version...
	if (sortedPinnedVersions.empty())
	{
		return nullptr;
	}
	else
	{
		// so, just return version with maximum "candidatness"
		return sortedPinnedVersions[0].version;
	}
}

const system::State* Cache::getSystemState() const
{
	return __impl->systemState.get();
}

bool Cache::isAutomaticallyInstalled(const string& packageName) const
{
	return __impl->extendedInfo.automaticallyInstalled.count(packageName);
}

vector< const BinaryVersion* >
Cache::getSatisfyingVersions(const RelationExpression& relationExpression) const
{
	return __impl->getSatisfyingVersions(relationExpression);
}

vector< const BinaryVersion* > Cache::getInstalledVersions() const
{
	vector< const BinaryVersion* > result;

	auto packageNames = __impl->systemState->getInstalledPackageNames();
	result.reserve(packageNames.size());
	for (const string& packageName: packageNames)
	{
		auto package = getBinaryPackage(packageName);
		if (!package)
		{
			fatal2i("unable to find the package '%s'", packageName);
		}
		auto version = package->getInstalledVersion();
		if (!version)
		{
			fatal2i("the package '%s' does not have installed version", packageName);
		}

		result.push_back(std::move(version));
	}

	return result;
}

const Cache::ExtendedInfo& Cache::getExtendedInfo() const
{
	return __impl->extendedInfo;
}

string Cache::getLocalizedDescription(const BinaryVersion* version) const
{
	return __impl->getLocalizedDescription(version);
}

string Cache::getPathOfCopyright(const BinaryVersion* version)
{
	if (!version->isInstalled())
	{
		return string();
	}

	return string("/usr/share/doc/") + version->packageName + "/copyright";
}

string Cache::getPathOfChangelog(const BinaryVersion* version)
{
	if (!version->isInstalled())
	{
		return string();
	}

	const string& packageName = version->packageName;
	const string commonPart = string("/usr/share/doc/") + packageName + "/";
	if (version->versionString.find('-') == string::npos)
	{
		return commonPart + "changelog.gz"; // non-native package
	}
	else
	{
		return commonPart + "changelog.Debian.gz"; // native package
	}
}

bool Cache::memoize = false;

} // namespace

