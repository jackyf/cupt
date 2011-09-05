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
#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/binarypackage.hpp>

#include <internal/filesystem.hpp>

#include <internal/worker/archives.hpp>

namespace cupt {
namespace internal {

ArchivesWorker::ArchivesWorker()
{
	__synchronize_apt_compat_symlinks();
}

void ArchivesWorker::__synchronize_apt_compat_symlinks()
{
	if (_config->getBool("cupt::worker::simulate"))
	{
		return;
	}

	auto archivesDirectory = _get_archives_directory();
	auto debPaths = fs::glob(archivesDirectory + "/*.deb");
	FORIT(debPathIt, debPaths)
	{
		const string& debPath = *debPathIt;
		if (!fs::fileExists(debPath))
		{
			// a dangling symlink
			if (unlink(debPath.c_str()) == -1)
			{
				warn("unable to delete dangling APT compatibility symbolic link '%s': EEE", debPath.c_str());
			}
		}
		else
		{
			// this is a regular file
			auto pathBasename = fs::filename(debPath);

			auto correctedBasename = pathBasename;
			auto offset = correctedBasename.find("%3a");
			if (offset != string::npos)
			{
				correctedBasename.replace(offset, 3, ":");
				auto correctedPath = archivesDirectory + "/" + correctedBasename;

				if (!fs::fileExists(correctedPath))
				{
					if (symlink(pathBasename.c_str(), correctedPath.c_str()) == -1)
					{
						fatal("unable to create APT compatibility symbolic link '%s' -> '%s': EEE",
								correctedPath.c_str(), pathBasename.c_str());
					}
				}
			}
		}
	}
}

vector< pair< string, shared_ptr< const BinaryVersion > > > ArchivesWorker::getArchivesInfo() const
{
	map< string, shared_ptr< const BinaryVersion > > knownArchives;

	auto archivesDirectory = _get_archives_directory();

	auto pathMaxLength = pathconf("/", _PC_PATH_MAX);
	vector< char > pathBuffer(pathMaxLength + 1, '\0');

	auto packageNames = _cache->getBinaryPackageNames();
	FORIT(packageNameIt, packageNames)
	{
		auto package = _cache->getBinaryPackage(*packageNameIt);
		if (!package)
		{
			continue;
		}

		auto versions = package->getVersions();
		FORIT(versionIt, versions)
		{
			auto path = archivesDirectory + '/' + _get_archive_basename(*versionIt);
			if (fs::fileExists(path))
			{
				knownArchives[path] = *versionIt;

				// checking for symlinks
				if (readlink(path.c_str(), &pathBuffer[0], pathMaxLength) == -1)
				{
					if (errno != EINVAL)
					{
						warn("readlink on '%s' failed: EEE", path.c_str());
					}
					// not a symlink
				}
				else
				{
					// a symlink (relative)
					string targetPath(archivesDirectory + '/' + &pathBuffer[0]);
					if (fs::fileExists(targetPath))
					{
						knownArchives[targetPath] = *versionIt;
					}
				}
			}
		}
	}

	auto paths = fs::glob(archivesDirectory + "/*.deb");

	vector< pair< string, shared_ptr< const BinaryVersion > > > result;

	FORIT(pathIt, paths)
	{
		shared_ptr< const BinaryVersion > version; // empty by default
		auto knownPathIt = knownArchives.find(*pathIt);
		if (knownPathIt != knownArchives.end())
		{
			version = knownPathIt->second;
		}
		result.push_back(make_pair(*pathIt, version));
	}

	return result;
}

void ArchivesWorker::deleteArchive(const string& path)
{
	// don't use ::realpath(), otherwise we won't delete symlinks
	auto archivesDirectory = _get_archives_directory();
	if (path.compare(0, archivesDirectory.size(), archivesDirectory))
	{
		fatal("path '%s' lies outside archives directory '%s'",
				path.c_str(), archivesDirectory.c_str());
	}
	if (path.find("/../") != string::npos)
	{
		fatal("path '%s' contains at least one '/../' substring", path.c_str());
	}

	if (!_config->getBool("cupt::worker::simulate"))
	{
		if (unlink(path.c_str()) == -1)
		{
			fatal("unable to delete file '%s': EEE", path.c_str());
		}
	}
	else
	{
		auto filename = fs::filename(path);
		simulate("deleting an archive '%s'", filename.c_str());
	}
}

void ArchivesWorker::deletePartialArchives()
{
	auto partialArchivesDirectory = _get_archives_directory() + partialDirectorySuffix;
	if (!fs::dirExists(partialArchivesDirectory))
	{
		return;
	}

	bool simulating = _config->getBool("cupt::worker::simulate");

	auto paths = fs::glob(partialArchivesDirectory + "/*");
	bool success = true;
	FORIT(pathIt, paths)
	{
		if (simulating)
		{
			auto filename = fs::filename(*pathIt);
			simulate("deleting a partial archive file '%s'", filename.c_str());
		}
		else
		{
			if (unlink(pathIt->c_str()) == -1)
			{
				success = false;
				warn("unable to delete file '%s': EEE", pathIt->c_str());
			}
		}
	}
	if (!success)
	{
		fatal("unable to delete partial archives");
	}
}

}
}

