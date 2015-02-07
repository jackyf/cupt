/**************************************************************************
*   Copyright (C) 2010-2011 by Eugene V. Lyubimkin                        *
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
#include <set>
#include <algorithm>

#include <cupt/system/snapshots.hpp>
#include <cupt/system/resolver.hpp>
#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/releaseinfo.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/file.hpp>
#include <cupt/versionstring.hpp>

#include <internal/common.hpp>
#include <internal/filesystem.hpp>

namespace cupt {

namespace internal {

class SnapshotsImpl
{
	shared_ptr< Config > __config;
	void loadVersionsIntoResolver(const string&, const Cache&, system::Resolver&);
 public:
	SnapshotsImpl(const shared_ptr< Config >& config);
	vector< string > getSnapshotNames() const;
	string getSnapshotsDirectory() const;
	string getSnapshotDirectory(const string&) const;
	void setupConfigForSnapshotOnly(const string& snapshotName);
	void setupResolverForSnapshotOnly(const string& snapshotName,
		const Cache&, system::Resolver&);
};

SnapshotsImpl::SnapshotsImpl(const shared_ptr< Config >& config)
	: __config(config)
{}

string SnapshotsImpl::getSnapshotsDirectory() const
{
	return __config->getPath("cupt::directory::state::snapshots");
}

string SnapshotsImpl::getSnapshotDirectory(const string& name) const
{
	return getSnapshotsDirectory() + '/' + name;
}

vector< string > SnapshotsImpl::getSnapshotNames() const
{
	vector< string > result;

	auto snapshotPaths = fs::glob(getSnapshotsDirectory() + "/*");
	for (const auto& path: snapshotPaths)
	{
		result.push_back(fs::filename(path));
	}

	return result;
}

void SnapshotsImpl::setupConfigForSnapshotOnly(const string& snapshotName)
{
	auto snapshotDirectory = getSnapshotDirectory(snapshotName);

	__config->setScalar("cupt::directory::state::lists", snapshotDirectory);
	__config->setScalar("dir::cache::archives", snapshotDirectory);

	__config->setScalar("dir::etc::sourcelist", snapshotDirectory + "/source");
	__config->setScalar("dir::etc::sourceparts", "/non-existent");
}

static void assertSnapshotPresent(const vector<string>& names, const string& name)
{
	if (std::find(names.begin(), names.end(), name) == names.end())
	{
		fatal2(__("unable to find a snapshot named '%s'"), name);
	}
}

static void assertSnapshotFormat(const string& snapshotDirectory)
{
	// currently we support none and '1'
	auto formatPath = snapshotDirectory + "/format";
	if (fs::fileExists(formatPath))
	{
		RequiredFile file(formatPath, "r");
		string content;
		file.getFile(content);
		chomp(content);
		if (content != "1")
		{
			fatal2(__("unsupported snapshot format '%s'"), content);
		}
	}
}

void SnapshotsImpl::setupResolverForSnapshotOnly(const string& snapshotName,
		const Cache& cache, system::Resolver& resolver)
{
	assertSnapshotPresent(getSnapshotNames(), snapshotName);

	auto snapshotDirectory = getSnapshotDirectory(snapshotName);

	assertSnapshotFormat(snapshotDirectory);

	loadVersionsIntoResolver(snapshotDirectory, cache, resolver);
}

static const BinaryVersion* findSnapshotVersion(const BinaryPackage* package)
{
	for (auto version: *package)
	{
		for (const auto& source: version->sources)
		{
			if (source.release->archive == "snapshot")
			{
				return version;
			}
		}
	}

	return nullptr;
}

static bool isReinstall(const BinaryVersion* version, const BinaryPackage* package)
{
	if (auto installedVersion = package->getInstalledVersion())
	{
		if (getOriginalVersionString(installedVersion->versionString).equal(version->versionString))
		{
			return true;
		}
	}
	return false;
}

void SnapshotsImpl::loadVersionsIntoResolver(
		const string& snapshotDirectory,
		const Cache& cache, system::Resolver& resolver)
{
	std::set< string > toBeInstalledPackageNames;

	{
		auto snapshotPackagesPath = snapshotDirectory + '/' + system::Snapshots::installedPackageNamesFilename;
		RequiredFile file(snapshotPackagesPath, "r");

		string packageName;
		while (!file.getLine(packageName).eof())
		{
			auto package = cache.getBinaryPackage(packageName);
			if (!package)
			{
				fatal2i("the package '%s' doesn't exist", packageName);
			}

			toBeInstalledPackageNames.insert(packageName);

			if (auto version = findSnapshotVersion(package))
			{
				if (!isReinstall(version, package))
				{
					resolver.installVersion({ version });
				}
			}
			else
			{
				fatal2i("unable to find snapshot version for the package '%s'", packageName);
			}
		}
	}

	for (const auto& packageName: cache.getBinaryPackageNames())
	{
		if (!toBeInstalledPackageNames.count(packageName))
		{
			resolver.removeVersions(cache.getBinaryPackage(packageName)->getVersions());
		}
	}
}

}

namespace system
{

Snapshots::Snapshots(const shared_ptr< Config >& config)
	: __impl(new internal::SnapshotsImpl(config))
{}

vector< string > Snapshots::getSnapshotNames() const
{
	return __impl->getSnapshotNames();
}

string Snapshots::getSnapshotsDirectory() const
{
	return __impl->getSnapshotsDirectory();
}

string Snapshots::getSnapshotDirectory(const string& name) const
{
	return __impl->getSnapshotDirectory(name);
}

void Snapshots::setupConfigForSnapshotOnly(const string& snapshotName)
{
	__impl->setupConfigForSnapshotOnly(snapshotName);
}

void Snapshots::setupResolverForSnapshotOnly(const string& snapshotName,
		const Cache& cache, Resolver& resolver)
{
	__impl->setupResolverForSnapshotOnly(snapshotName, cache, resolver);
}

Snapshots::~Snapshots()
{
	delete __impl;
}

const string Snapshots::installedPackageNamesFilename = "installed_package_names";

}
}

