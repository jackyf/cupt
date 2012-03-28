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
				warn2e(__("unable to remove the dangling APT compatibility symbolic link '%s'"), debPath);
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
						fatal2e(__("unable to create the APT compatibility symbolic link '%s' -> '%s'"),
								correctedPath, pathBasename);
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
				auto readlinkResult = readlink(path.c_str(), &pathBuffer[0], pathMaxLength);
				if (readlinkResult == -1)
				{
					if (errno != EINVAL)
					{
						warn2e(__("%s() failed: '%s'"), "readlink", path);
					}
					// not a symlink
				}
				else
				{
					// a symlink (relative)
					string relativePath(pathBuffer.begin(), pathBuffer.begin() + readlinkResult);
					string targetPath(archivesDirectory + '/' + relativePath);
					if (fs::fileExists(targetPath))
					{
						knownArchives[targetPath] = *versionIt;
					}
				}
			}
		}
	}

	auto paths = fs::lglob(archivesDirectory, "*.deb");

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
		fatal2(__("the path '%s' lies outside the archives directory '%s'"), path, archivesDirectory);
	}
	if (path.find("/../") != string::npos)
	{
		fatal2(__("the path '%s' contains at least one '/../' substring"), path);
	}

	if (!_config->getBool("cupt::worker::simulate"))
	{
		if (unlink(path.c_str()) == -1)
		{
			fatal2e(__("unable to remove the file '%s'"), path);
		}
	}
	else
	{
		auto filename = fs::filename(path);
		simulate2("deleting an archive '%s'", filename);
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
			simulate2("deleting a partial archive file '%s'", filename);
		}
		else
		{
			if (unlink(pathIt->c_str()) == -1)
			{
				success = false;
				warn2e(__("unable to remove the file '%s'"), (*pathIt));
			}
		}
	}
	if (!success)
	{
		fatal2(__("unable to remove partial archives"));
	}
}

}
}

