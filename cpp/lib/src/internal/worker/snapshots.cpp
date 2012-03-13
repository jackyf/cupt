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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include <algorithm>

#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/system/snapshots.hpp>
#include <cupt/system/state.hpp>
#include <cupt/file.hpp>

#include <internal/filesystem.hpp>

#include <internal/worker/snapshots.hpp>

namespace cupt {
namespace internal {

void SnapshotsWorker::__delete_temporary(const string& directory, bool warnOnly)
{
	_logger->log(Logger::Subsystem::Snapshots, 2,
			format2("deleting a partial snapshot directory '%s'", directory));
	try
	{
		string command = string("rm -r ") + directory;
		_run_external_command(Logger::Subsystem::Snapshots, command.c_str());
	}
	catch (Exception&)
	{
		if (warnOnly)
		{
			warn2("unable to remove partial snapshot directory '%s'", directory);
		}
		else
		{
			_logger->loggedFatal2(Logger::Subsystem::Snapshots, 2,
					format2, "unable to remove partial snapshot directory '%s'", directory);
		}
	}
}

void createTextFile(const string& path, const vector< string >& lines,
		Logger* logger, bool simulating)
{
	logger->log(Logger::Subsystem::Snapshots, 3,
			format2("creating the file '%s'", path));
	if (simulating)
	{
		simulate2("writing file '%s'", path);
	}
	else
	{
		string openError;
		File file(path, "w", openError);
		if (!openError.empty())
		{
			logger->loggedFatal2(Logger::Subsystem::Snapshots, 3,
					format2, "unable to open the file '%s' for writing: %s", path, openError);
		}

		FORIT(lineIt, lines)
		{
			file.put(*lineIt);
			file.put("\n", 1);
		}
	}
};

void SnapshotsWorker::__do_repacks(const vector< string >& installedPackageNames,
		bool simulating)
{
	FORIT(packageNameIt, installedPackageNames)
	{
		const string& packageName = *packageNameIt;

		_logger->log(Logger::Subsystem::Snapshots, 2,
				format2("repacking the installed package '%s'", packageName));

		try
		{
			auto package = _cache->getBinaryPackage(packageName);
			if (!package)
			{
				_logger->loggedFatal2(Logger::Subsystem::Snapshots, 2,
						format2, "internal error: no binary package '%s'", packageName);
			}
			auto version = package->getInstalledVersion();
			if (!version)
			{
				_logger->loggedFatal2(Logger::Subsystem::Snapshots, 2,
						format2, "internal error: no installed version for the installed package '%s'", packageName);
			}
			const string& architecture = version->architecture;

			_run_external_command(Logger::Subsystem::Snapshots, format2("dpkg-repack --arch=%s %s",
						architecture, packageName));

			/* dpkg-repack uses dpkg-deb -b, which produces file in format

			   <package_name>_<stripped_version_string>_<arch>.deb

			   I can't say why the hell someone decided to strip version here,
			   so I have to rename the file properly.
			*/
			if (!simulating)
			{
				// find a file
				auto files = fs::glob(packageName + "_*.deb");
				if (files.size() != 1)
				{
					_logger->loggedFatal2(Logger::Subsystem::Snapshots, 2,
							format2, "dpkg-repack produced either no or more than one Debian archive for the package '%s'",
							packageName);
				}
				const string& badFilename = files[0];
				auto goodFilename = format2("%s_%s_%s.deb", packageName,
						version->versionString, architecture);

				if (!fs::move(badFilename, goodFilename))
				{
					_logger->loggedFatal2(Logger::Subsystem::Snapshots, 3,
							format2e, "unable to rename '%s' to '%s'", badFilename, goodFilename);
				}
			}
		}
		catch (...)
		{
			_logger->loggedFatal2(Logger::Subsystem::Snapshots, 2,
					format2, "failed to repack the package '%s'", packageName);
		}
	}
}

string SnapshotsWorker::__create_index_file(const Cache::IndexEntry& indexEntry)
{
	auto filename = fs::filename(_cache->getPathOfIndexList(indexEntry));

	_logger->log(Logger::Subsystem::Snapshots, 2, "building an index file");
	_run_external_command(Logger::Subsystem::Snapshots, string("dpkg-scanpackages . > ") + filename);
	return filename;
}

void SnapshotsWorker::__create_release_file(const string& temporarySnapshotDirectory,
		const string& snapshotName, const string& indexFilename,
		const Cache::IndexEntry& indexEntry,bool simulating)
{
	_logger->log(Logger::Subsystem::Snapshots, 2, "building a Release file");
	vector< string > lines;

#define LL(x) lines.push_back(x)
	LL("Origin: Cupt");
	LL("Label: snapshot");
	LL("Suite: snapshot");
	LL("Codename: snapshot");

	{
		auto previousLocaleTime = setlocale(LC_TIME, "C");

		struct tm brokenDownTime;
		time_t unixTime = time(NULL);
		char timeBuf[128];
		if (!strftime(timeBuf, sizeof(timeBuf), "%a, %d %b %Y %H:%M:%S UTC", gmtime_r(&unixTime, &brokenDownTime)))
		{
			_logger->loggedFatal2(Logger::Subsystem::Snapshots, 2, format2e, "strftime failed");
		}
		LL(string("Date: ") + timeBuf);

		setlocale(LC_TIME, previousLocaleTime);
	}

	LL("Architectures: all " + _config->getString("apt::architecture"));
	LL(format2("Description: Cupt-made system snapshot '%s'", snapshotName));

	if (!simulating)
	{
		HashSums indexHashSums;
		indexHashSums.fill(indexFilename);

		auto size = fs::fileSize(indexFilename);

		LL("MD5Sum:");
	    LL(format2(" %s %zu Packages", indexHashSums[HashSums::MD5], size));
		LL("SHA1:");
	    LL(format2(" %s %zu Packages", indexHashSums[HashSums::SHA1], size));
		LL("SHA256:");
	    LL(format2(" %s %zu Packages", indexHashSums[HashSums::SHA256], size));
	}
#undef LL

	auto path = temporarySnapshotDirectory + '/' +
			fs::filename(_cache->getPathOfReleaseList(indexEntry));
	createTextFile(path, lines, _logger, simulating);
}

void checkSnapshotName(const Snapshots& snapshots, const string& name)
{
	if (name.empty())
	{
		fatal2(__("the system snapshot name cannot be empty"));
	}
	if (name[0] == '.')
	{
		fatal2(__("the system snapshot name '%s' cannot start with a '.'"), name);
	}

	{
		auto existingNames = snapshots.getSnapshotNames();
		if (std::find(existingNames.begin(), existingNames.end(), name) != existingNames.end())
		{
			fatal2(__("the system snapshot named '%s' already exists"), name);
		}
	}
}

void checkSnapshotSavingTools()
{
	// ensuring needed tools is available
	if (::system("which dpkg-repack >/dev/null 2>/dev/null"))
	{
		fatal2(__("the 'dpkg-repack' binary is not available, install the package 'dpkg-repack'"));
	}
	if (::system("which dpkg-scanpackages >/dev/null 2>/dev/null"))
	{
		fatal2(__("the 'dpkg-scanpackages' binary is not available, install the package 'dpkg-dev'"));
	}

}

void SnapshotsWorker::saveSnapshot(const Snapshots& snapshots, const string& name)
{
	checkSnapshotName(snapshots, name);
	checkSnapshotSavingTools();

	_logger->log(Logger::Subsystem::Snapshots, 1,
			format2("saving the system snapshot under the name '%s'", name));

	auto snapshotsDirectory = snapshots.getSnapshotsDirectory();
	auto snapshotDirectory = snapshots.getSnapshotDirectory(name);
	auto temporarySnapshotDirectory = snapshots.getSnapshotDirectory(string(".partial-") + name);

	auto simulating = _config->getBool("cupt::worker::simulate");

	if (!simulating)
	{
		// creating snapshot directory
		if (!fs::dirExists(snapshotsDirectory))
		{
			if (mkdir(snapshotsDirectory.c_str(), 0755) == -1)
			{
				_logger->loggedFatal2(Logger::Subsystem::Snapshots, 2,
						format2e, "unable to create the snapshots directory '%s'", snapshotsDirectory);
			}
		}
		if (fs::dirExists(temporarySnapshotDirectory))
		{
			// some leftover from previous tries
			__delete_temporary(temporarySnapshotDirectory, false);
		}
		if (mkdir(temporarySnapshotDirectory.c_str(), 0755) == -1)
		{
			_logger->loggedFatal2(Logger::Subsystem::Snapshots, 2,
					format2e, "unable to create a temporary snapshot directory '%s'", temporarySnapshotDirectory);
		}
	}

	auto installedPackageNames = _cache->getSystemState()->getInstalledPackageNames();
	try
	{
		{
			_logger->log(Logger::Subsystem::Snapshots, 2, "creating snapshot information files");

			// snapshot format version
			createTextFile(temporarySnapshotDirectory + "/format", vector< string >{ "1" },
					_logger, simulating);

			// saving list of package names
			createTextFile(temporarySnapshotDirectory + "/" + Snapshots::installedPackageNamesFilename,
					installedPackageNames, _logger, simulating);

			{ // building source line
				auto sourceLine = format2("deb file://%s %s/", snapshotsDirectory, name);
				createTextFile(temporarySnapshotDirectory + "/source", vector< string >{ sourceLine },
						_logger, simulating);
			}
		}

		auto currentDirectoryFd = open(".", O_RDONLY);
		if (currentDirectoryFd == -1)
		{
			_logger->loggedFatal2(Logger::Subsystem::Snapshots, 2,
					format2e, "unable to open the current directory");
		}

		if (!simulating)
		{
			if (chdir(temporarySnapshotDirectory.c_str()) == -1)
			{
				_logger->loggedFatal2(Logger::Subsystem::Snapshots, 2,
						format2e, "unable to set current directory to '%s'", temporarySnapshotDirectory);
			}
		}

		__do_repacks(installedPackageNames, simulating);

		Cache::IndexEntry indexEntry; // component remains empty, "easy" source type
		indexEntry.category = Cache::IndexEntry::Binary;
		indexEntry.uri = string("file://") + snapshotsDirectory;
		indexEntry.distribution = name;

		string indexFilename = __create_index_file(indexEntry);
		__create_release_file(temporarySnapshotDirectory, name,
				indexFilename, indexEntry, simulating);

		if (!simulating)
		{
			if (fchdir(currentDirectoryFd) == -1)
			{
				_logger->loggedFatal2(Logger::Subsystem::Snapshots, 2,
						format2e, "unable to return to previous working directory");
			}

			// all done, do final move
			if (!fs::move(temporarySnapshotDirectory, snapshotDirectory))
			{
				_logger->loggedFatal2(Logger::Subsystem::Snapshots, 2,
						format2e, "unable to rename '%s' to '%s'",
						temporarySnapshotDirectory, snapshotDirectory);
			}
		}
	}
	catch (Exception&)
	{
		// deleting partially constructed snapshot (try)
		if (chdir(snapshotsDirectory.c_str()) == -1)
		{
			warn2e(__("unable to set current directory to '%s'"), snapshotsDirectory);
		}

		try
		{
			_run_external_command(Logger::Subsystem::Snapshots,
					string("rm -r " + temporarySnapshotDirectory));
		}
		catch (...)
		{
			warn2(__("unable to remove partial snapshot directory '%s'"),
					temporarySnapshotDirectory);
		}

		_logger->loggedFatal2(Logger::Subsystem::Snapshots, 1,
				format2, "error constructing system snapshot named '%s'", name);
	}
}

void SnapshotsWorker::renameSnapshot(const Snapshots& snapshots,
		const string& previousName, const string& newName)
{
	auto snapshotNames = snapshots.getSnapshotNames();
	if (std::find(snapshotNames.begin(), snapshotNames.end(), previousName)
			== snapshotNames.end())
	{
		fatal2(__("unable to find a snapshot named '%s'"), previousName);
	}
	if (std::find(snapshotNames.begin(), snapshotNames.end(), newName)
			!= snapshotNames.end())
	{
		fatal2(__("the snapshot named '%s' already exists"), newName);
	}

	auto previousSnapshotDirectory = snapshots.getSnapshotDirectory(previousName);
	auto newSnapshotDirectory = snapshots.getSnapshotDirectory(newName);

	_logger->log(Logger::Subsystem::Snapshots, 1,
			format2("renaming the snapshot from '%s' to '%s'", previousName, newName));
	_run_external_command(Logger::Subsystem::Snapshots, format2("mv %s %s",
			previousSnapshotDirectory, newSnapshotDirectory));
}

void checkLooksLikeSnapshot(const string& directory)
{
	if (!fs::fileExists(directory + '/' + Snapshots::installedPackageNamesFilename))
	{
		fatal2(__("'%s' is not a valid snapshot"), directory);
	}
}

void SnapshotsWorker::removeSnapshot(const Snapshots& snapshots, const string& name)
{
	auto snapshotNames = snapshots.getSnapshotNames();
	if (std::find(snapshotNames.begin(), snapshotNames.end(), name)
			== snapshotNames.end())
	{
		fatal2(__("unable to find a snapshot named '%s'"), name);
	}

	auto snapshotDirectory = snapshots.getSnapshotDirectory(name);
	checkLooksLikeSnapshot(snapshotDirectory);

	_logger->log(Logger::Subsystem::Snapshots, 1,
			format2("removing the snapshot '%s'", name));
	_run_external_command(Logger::Subsystem::Snapshots,
			string("rm -r ") + snapshotDirectory);
}

}
}

