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
#include <sys/types.h>
#include <sys/wait.h>

#include <cupt/config.hpp>
#include <cupt/download/uri.hpp>
#include <cupt/download/manager.hpp>

#include <internal/filesystem.hpp>
#include <internal/lock.hpp>

#include <internal/worker/metadata.hpp>

namespace cupt {
namespace internal {

string MetadataWorker::__get_indexes_directory() const
{
	return _config->getPath("dir::state::lists");
}

string getDownloadPath(const string& targetPath)
{
	return fs::dirname(targetPath) + WorkerBase::partialDirectorySuffix +
			"/" + fs::filename(targetPath);
}

string getUriBasename(const string& uri)
{
	auto slashPosition = uri.rfind('/');
	if (slashPosition != string::npos)
	{
		return uri.substr(slashPosition + 1);
	}
	else
	{
		return uri;
	}
}

string getFilenameExtension(const string& source)
{
	auto position = source.find_last_of("./");
	if (position != string::npos && source[position] == '.')
	{
		return source.substr(position);
	}
	else
	{
		return string();
	}
};

std::function< string () > generateMovingSub(const string& downloadPath, const string& targetPath)
{
	return [downloadPath, targetPath]() -> string
	{
		return fs::move(downloadPath, targetPath);
	};
};

bool generateUncompressingSub(const download::Uri& uri, const string& downloadPath,
		const string& targetPath, std::function< string () >& sub)
{
	auto filenameExtension = getFilenameExtension(uri);

	// checking and preparing unpackers
	if (filenameExtension == ".lzma" || filenameExtension == ".bz2" || filenameExtension == ".gz")
	{
		string uncompressorName;
		if (filenameExtension == ".lzma")
		{
			uncompressorName = "unlzma";
		}
		else if (filenameExtension == ".bz2")
		{
			uncompressorName = "bunzip2";
		}
		else if (filenameExtension == ".gz")
		{
			uncompressorName = "gunzip";
		}
		else
		{
			fatal("internal error: extension '%s' has no uncompressor", filenameExtension.c_str());
		}

		if (::system(sf("which %s >/dev/null", uncompressorName.c_str()).c_str()))
		{
			warn("'%s' uncompressor is not available, not downloading '%s'",
					uncompressorName.c_str(), string(uri).c_str());
			return false;
		}

		sub = [uncompressorName, downloadPath, targetPath]() -> string
		{
			auto uncompressingResult = ::system(sf("%s %s -c > %s",
					uncompressorName.c_str(), downloadPath.c_str(), targetPath.c_str()).c_str());
			// anyway, remove the compressed file, ignoring errors if any
			unlink(downloadPath.c_str());
			if (uncompressingResult)
			{
				return sf(__("failed to uncompress '%s', '%s' returned error %d"),
						downloadPath.c_str(), uncompressorName.c_str(), uncompressingResult);
			}
			return string(); // success
		};
		return true;
	}
	else if (filenameExtension.empty())
	{
		// no extension
		sub = generateMovingSub(downloadPath, targetPath);
		return true;
	}
	else
	{
		warn("unknown file extension '%s', not downloading '%s'",
					filenameExtension.c_str(), string(uri).c_str());
		return false;
	}
};

bool MetadataWorker::__update_release(download::Manager& downloadManager,
		const Cache::IndexEntry& indexEntry, bool& releaseFileChanged)
{
	auto targetPath = _cache->getPathOfReleaseList(indexEntry);

	// we'll check hash sums of local file before and after to
	// determine do we need to clean partial indexes
	//
	HashSums hashSums; // empty now
	hashSums[HashSums::MD5] = "0"; // won't match for sure
	if (fs::fileExists(targetPath))
	{
		// the Release file already present
		hashSums.fill(targetPath);
	}
	releaseFileChanged = false; // until proved otherwise later

	// downloading Release file
	auto alias = indexEntry.distribution + ' ' + "Release";
	auto longAlias = indexEntry.uri + ' ' + alias;

	auto uri = _cache->getDownloadUriOfReleaseList(indexEntry);
	auto downloadPath = getDownloadPath(targetPath);

	{
		download::Manager::DownloadEntity downloadEntity;

		download::Manager::ExtendedUri extendedUri(download::Uri(uri),
				alias, longAlias);

		downloadEntity.extendedUris.push_back(std::move(extendedUri));
		downloadEntity.targetPath = downloadPath;
		downloadEntity.postAction = generateMovingSub(downloadPath, targetPath);
		downloadEntity.size = (size_t)-1;

		auto downloadResult = downloadManager.download(
				vector< download::Manager::DownloadEntity >{ downloadEntity });
		if (!downloadResult.empty())
		{
			return false;
		}
	}

	releaseFileChanged = !hashSums.verify(targetPath);

	// downloading signature for Release file
	auto signatureUri = uri + ".gpg";
	auto signatureTargetPath = targetPath + ".gpg";
	auto signatureDownloadPath = downloadPath + ".gpg";

	auto signatureAlias = alias + ".gpg";

	auto signaturePostAction = generateMovingSub(signatureDownloadPath, signatureTargetPath);

	bool simulating = _config->getBool("cupt::worker::simulate");
	if (!simulating and !_config->getBool("cupt::update::keep-bad-signatures"))
	{
		// if we have to check signature prior to moving to canonical place
		// (for compatibility with APT tools) and signature check failed,
		// delete the downloaded file
		auto oldSignaturePostAction = signaturePostAction;
		signaturePostAction = [oldSignaturePostAction, longAlias, targetPath, signatureTargetPath, &_config]() -> string
		{
			auto moveError = oldSignaturePostAction();
			if (!moveError.empty())
			{
				return moveError;
			}

			if (!Cache::verifySignature(_config, targetPath))
			{
				if (unlink(signatureTargetPath.c_str()) == -1)
				{
					warn("unable to delete file '%s': EEE", signatureTargetPath.c_str());
				}
				warn("signature verification for '%s' failed", longAlias.c_str());
			}
			return string();
		};
	}

	{
		download::Manager::DownloadEntity downloadEntity;

		download::Manager::ExtendedUri extendedUri(download::Uri(signatureUri),
				signatureAlias, indexEntry.uri + ' ' + signatureAlias);

		downloadEntity.extendedUris.push_back(std::move(extendedUri));
		downloadEntity.targetPath = signatureDownloadPath;
		downloadEntity.postAction = signaturePostAction;
		downloadEntity.size = (size_t)-1;

		auto downloadResult = downloadManager.download(
				vector< download::Manager::DownloadEntity >{ downloadEntity });
		if (!downloadResult.empty())
		{
			return false;
		}
	}

	return true;
}

ssize_t MetadataWorker::__get_uri_priority(const string& uri)
{
	auto extension = getFilenameExtension(uri);
	if (extension.empty())
	{
		extension = "uncompressed";
	}
	else if (extension[0] == '.') // will be true probably in all cases
	{
		extension = extension.substr(1); // remove starting '.' if exist
	}
	auto variableName = string("cupt::update::compression-types::") +
			extension + "::priority";
	return _config->getInteger(variableName);
}

bool MetadataWorker::__update_index(download::Manager& downloadManager,
		const Cache::IndexEntry& indexEntry, bool releaseFileChanged, bool& indexFileChanged)
{
	// downloading Packages/Sources
	auto targetPath = _cache->getPathOfIndexList(indexEntry);
	auto downloadInfo = _cache->getDownloadInfoOfIndexList(indexEntry);

	indexFileChanged = true;

	// checking maybe there is no difference between the local and the remote?
	bool simulating = _config->getBool("cupt::worker::simulate");
	if (!simulating && fs::fileExists(targetPath))
	{
		FORIT(downloadRecordIt, downloadInfo)
		{
			if (downloadRecordIt->hashSums.verify(targetPath))
			{
				// yeah, really
				indexFileChanged = false;
				return true;
			}
		}
	}

	{ // sort download files by priority and size
		auto comparator = [this](const Cache::IndexDownloadRecord& left, const Cache::IndexDownloadRecord& right)
		{
			auto leftPriority = this->__get_uri_priority(left.uri);
			auto rightPriority = this->__get_uri_priority(right.uri);
			if (leftPriority == rightPriority)
			{
				return left.size < right.size;
			}
			else
			{
				return (leftPriority > rightPriority);
			}
		};
		std::sort(downloadInfo.begin(), downloadInfo.end(), comparator);
	}

	auto baseDownloadPath = getDownloadPath(targetPath);
	string downloadError;
	FORIT(downloadRecordIt, downloadInfo)
	{
		const string& uri = downloadRecordIt->uri;
		auto downloadPath = baseDownloadPath + getFilenameExtension(uri);

		std::function< string () > uncompressingSub;
		if (!generateUncompressingSub(uri, downloadPath, targetPath, uncompressingSub))
		{
			continue;
		}

		auto alias = indexEntry.distribution + '/' + indexEntry.component +
				' ' + getUriBasename(uri);

		if (!simulating)
		{
			// here we check for outdated dangling indexes in partial directory
			if (releaseFileChanged && fs::fileExists(downloadPath))
			{
				if (unlink(downloadPath.c_str()) == -1)
				{
					warn("unable to remove outdated partial index file '%s': EEE",
							downloadPath.c_str());
				}
			}
		}

		download::Manager::DownloadEntity downloadEntity;

		download::Manager::ExtendedUri extendedUri(download::Uri(uri),
				alias, indexEntry.uri + ' ' + alias);
		downloadEntity.extendedUris.push_back(std::move(extendedUri));
		downloadEntity.targetPath = downloadPath;
		downloadEntity.size = downloadRecordIt->size;

		auto hashSums = downloadRecordIt->hashSums;
		downloadEntity.postAction = [&hashSums, &downloadPath, &uncompressingSub]() -> string
		{
			if (!hashSums.verify(downloadPath))
			{
				if (unlink(downloadPath.c_str()) == -1)
				{
					warn("unable to remove partial index file '%s': EEE", downloadPath.c_str());
				}
				return __("hash sums mismatch");
			}
			return uncompressingSub();
		};
		auto downloadError = downloadManager.download(
				vector< download::Manager::DownloadEntity >{ downloadEntity });
		if (downloadError.empty())
		{
			return true;
		}
	}

	// we reached here if neither download URI succeeded
	warn("failed to download index for '%s/%s'",
			indexEntry.distribution.c_str(), indexEntry.component.c_str());
	return false;
}

void MetadataWorker::__update_translations(download::Manager& downloadManager,
		const Cache::IndexEntry& indexEntry, bool indexFileChanged)
{
	bool simulating = _config->getBool("cupt::worker::simulate");
	// downloading file containing localized descriptions
	auto downloadInfo = _cache->getDownloadInfoOfLocalizedDescriptions(indexEntry);
	string downloadError;
	FORIT(downloadRecordIt, downloadInfo)
	{
		const string& uri = downloadRecordIt->uri;
		const string& targetPath = downloadRecordIt->localPath;

		auto downloadPath = getDownloadPath(targetPath) + getFilenameExtension(uri);

		std::function< string () > uncompressingSub;
		if (!generateUncompressingSub(uri, downloadPath, targetPath, uncompressingSub))
		{
			continue;
		}

		auto alias = indexEntry.distribution + '/' + indexEntry.component +
				' ' + getUriBasename(uri);

		if (!simulating)
		{
			// here we check for outdated dangling files in partial directory
			if (indexFileChanged && fs::fileExists(downloadPath))
			{
				if (unlink(downloadPath.c_str()) == -1)
				{
					warn("unable to remove outdated partial index localization file '%s': EEE",
							downloadPath.c_str());
				}
			}
		}

		download::Manager::DownloadEntity downloadEntity;

		download::Manager::ExtendedUri extendedUri(download::Uri(uri),
				alias, indexEntry.uri + ' ' + alias);
		downloadEntity.extendedUris.push_back(std::move(extendedUri));
		downloadEntity.targetPath = downloadPath;
		downloadEntity.size = (size_t)-1;
		downloadEntity.postAction = uncompressingSub;

		auto downloadError = downloadManager.download(
				vector< download::Manager::DownloadEntity >{ downloadEntity });
		if (downloadError.empty())
		{
			return;
		}
	}
}

bool MetadataWorker::__update_release_and_index_data(download::Manager& downloadManager,
		const Cache::IndexEntry& indexEntry)
{
	// phase 1
	bool releaseFileChanged;
	if (!__update_release(downloadManager, indexEntry, releaseFileChanged))
	{
		return false;
	}

	// phase 2
	bool indexFileChanged;
	if (!__update_index(downloadManager, indexEntry, releaseFileChanged, indexFileChanged))
	{
		return false;
	}

	__update_translations(downloadManager, indexEntry, indexFileChanged);
	return true;
}

void MetadataWorker::updateReleaseAndIndexData(const shared_ptr< download::Progress >& downloadProgress)
{
	auto indexesDirectory = __get_indexes_directory();
	bool simulating = _config->getBool("cupt::worker::simulate");

	shared_ptr< internal::Lock > lock;
	if (!simulating)
	{
		lock.reset(new internal::Lock(_config, indexesDirectory + "/lock"));
	}

	{ // run pre-actions
		auto preCommands = _config->getList("apt::update::pre-invoke");
		FORIT(commandIt, preCommands)
		{
			auto errorId = sf("pre-invoke action '%s'", commandIt->c_str());
			_run_external_command(*commandIt, errorId);
		}
	}

	if (!simulating)
	{
		// unconditional clearing of partial chunks of Release[.gpg] files
		auto partialIndexesDirectory = indexesDirectory + partialDirectorySuffix;
		auto paths = fs::glob(partialIndexesDirectory + "/*Release*");
		FORIT(pathIt, paths)
		{
			unlink(pathIt->c_str()); // without error-checking, yeah
		}

		// also create directory if it doesn't exist
		if (! fs::dirExists(partialIndexesDirectory))
		{
			if (mkdir(partialIndexesDirectory.c_str(), 0755) == -1)
			{
				fatal("unable to create partial directory '%s': EEE", partialIndexesDirectory.c_str());
			}
		}
	}

	int masterExitCode = true;
	{ // download manager involved part
		download::Manager downloadManager(_config, downloadProgress);

		set< int > pids;

		auto indexEntries = _cache->getIndexEntries();
		FORIT(indexEntryIt, indexEntries)
		{
			auto pid = fork();
			if (pid == -1)
			{
				fatal("fork failed: EEE");
			}

			if (pid)
			{
				// master process
				pids.insert(pid);
			}
			else
			{
				// child process
				bool success; // bad by default

				// wrapping all errors here
				try
				{
					success = __update_release_and_index_data(downloadManager, *indexEntryIt);
				}
				catch (...)
				{
					success = false;
				}
				_exit(success ? 0 : EXIT_FAILURE);
			}
		}
		while (!pids.empty())
		{
			int status;
			pid_t pid = wait(&status);
			if (pid == -1)
			{
				fatal("wait failed: EEE");
			}
			pids.erase(pid);
			// if something went bad in child, the parent won't return non-zero code too
			if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			{
				masterExitCode = false;
			}
		}
	};

	{ // run post-actions
		auto postCommands = _config->getList("apt::update::post-invoke");
		FORIT(commandIt, postCommands)
		{
			auto errorId = sf("post-invoke action '%s'", commandIt->c_str());
			_run_external_command(*commandIt, errorId);
		}
		if (masterExitCode)
		{
			auto postSuccessCommands = _config->getList("apt::update::post-invoke-success");
			FORIT(commandIt, postSuccessCommands)
			{
				auto errorId = sf("post-invoke-success action '%s'", commandIt->c_str());
				_run_external_command(*commandIt, errorId);
			}
		}
	}

	lock.reset();

	if (!masterExitCode)
	{
		fatal("there were errors while downloading release and index data");
	}
}

}
}

