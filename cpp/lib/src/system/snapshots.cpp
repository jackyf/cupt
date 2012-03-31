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

#include <internal/common.hpp>
#include <internal/filesystem.hpp>

namespace cupt {

namespace internal {

class SnapshotsImpl
{
	shared_ptr< Config > __config;
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

	FORIT(pathIt, snapshotPaths)
	{
		result.push_back(fs::filename(*pathIt));
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

void SnapshotsImpl::setupResolverForSnapshotOnly(const string& snapshotName,
		const Cache& cache, system::Resolver& resolver)
{
	{
		auto snapshotNames = getSnapshotNames();
		if (std::find(snapshotNames.begin(), snapshotNames.end(), snapshotName)
				== snapshotNames.end())
		{
			fatal2(__("unable to find a snapshot named '%s'"), snapshotName);
		}
	}

	auto snapshotDirectory = getSnapshotDirectory(snapshotName);

	{ // checking snapshot format, current we support none and '1'
		auto formatPath = snapshotDirectory + "/format";
		if (fs::fileExists(formatPath))
		{
			string openError;
			File file(formatPath, "r", openError);
			if (!openError.empty())
			{
				fatal2(__("unable to open the format file '%s': %s"), formatPath, openError);
			}
			string content;
			file.getFile(content);
			chomp(content);
			if (content != "1")
			{
				fatal2(__("unsupported snapshot format '%s'"), content);
			}
		}
	}

	std::set< string > toBeInstalledPackageNames;

	{
		auto snapshotPackagesPath = snapshotDirectory + '/' + system::Snapshots::installedPackageNamesFilename;
		string openError;
		File file(snapshotPackagesPath, "r", openError);
		if (!openError.empty())
		{
			fatal2(__("unable to open the file '%s': %s"), snapshotPackagesPath, openError);
		}

		string packageName;
		while (!file.getLine(packageName).eof())
		{
			auto package = cache.getBinaryPackage(packageName);
			if (!package)
			{
				fatal2i("the package '%s' doesn't exist", packageName);
			}

			toBeInstalledPackageNames.insert(packageName);

			auto versions = package->getVersions();
			FORIT(versionIt, versions)
			{
				FORIT(sourceIt, (*versionIt)->sources)
				{
					if (sourceIt->release->archive == "snapshot")
					{
						resolver.installVersion(*versionIt);
						goto next_file_line;
					}
				}
			}

			// not found
			fatal2i("unable to find snapshot version for the package '%s'", packageName);

			next_file_line:
			;
		}
	}

	auto allPackageNames = cache.getBinaryPackageNames();
	FORIT(packageNameIt, allPackageNames)
	{
		if (!toBeInstalledPackageNames.count(*packageNameIt))
		{
			resolver.removePackage(*packageNameIt);
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

