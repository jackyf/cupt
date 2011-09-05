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

#include <algorithm>

#include <common/regex.hpp>

#include <cupt/config.hpp>
#include <cupt/download/uri.hpp>
#include <cupt/download/manager.hpp>
#include <cupt/file.hpp>

#include <internal/filesystem.hpp>
#include <internal/lock.hpp>
#include <internal/cachefiles.hpp>
#include <internal/tagparser.hpp>
#include <internal/common.hpp>

#include <internal/worker/metadata.hpp>

namespace cupt {
namespace internal {

string MetadataWorker::__get_indexes_directory() const
{
	return _config->getPath("cupt::directory::state::lists");
}

string getDownloadPath(const string& targetPath)
{
	return fs::dirname(targetPath) + WorkerBase::partialDirectorySuffix +
			"/" + fs::filename(targetPath);
}

string getUriBasename(const string& uri, bool skipFirstSlash = false)
{
	auto slashPosition = uri.rfind('/');
	if (slashPosition != string::npos)
	{
		if (skipFirstSlash && slashPosition != 0)
		{
			auto secondSlashPosition = uri.rfind('/', slashPosition-1);
			if (secondSlashPosition != string::npos)
			{
				slashPosition = secondSlashPosition;
			}
		}
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
	if (filenameExtension == ".xz" || filenameExtension == ".lzma" || filenameExtension == ".bz2" || filenameExtension == ".gz")
	{
		string uncompressorName;
		if (filenameExtension == ".xz")
		{
			uncompressorName = "unxz";
		}
		else if (filenameExtension == ".lzma")
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

string __get_pidded_string(const string& input)
{
	return sf("(%d): %s", getpid(), input.c_str());
}
string __get_download_log_message(const string& longAlias)
{
	return __get_pidded_string(string("downloading: ") + longAlias);
}

bool MetadataWorker::__update_release(download::Manager& downloadManager,
		const Cache::IndexEntry& indexEntry, bool& releaseFileChanged)
{
	bool simulating = _config->getBool("cupt::worker::simulate");
	bool runChecks = _config->getBool("cupt::update::check-release-files");

	auto targetPath = cachefiles::getPathOfReleaseList(*_config, indexEntry);

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
	_logger->log(Logger::Subsystem::Metadata, 3, __get_download_log_message(longAlias));

	auto uri = cachefiles::getDownloadUriOfReleaseList(indexEntry);
	auto downloadPath = getDownloadPath(targetPath);

	{
		download::Manager::DownloadEntity downloadEntity;

		download::Manager::ExtendedUri extendedUri(download::Uri(uri),
				alias, longAlias);

		downloadEntity.extendedUris.push_back(std::move(extendedUri));
		downloadEntity.targetPath = downloadPath;
		downloadEntity.postAction = generateMovingSub(downloadPath, targetPath);
		downloadEntity.size = (size_t)-1;

		if (!simulating && runChecks)
		{
			auto oldPostAction = downloadEntity.postAction;
			auto extendedPostAction = [oldPostAction, _config, targetPath]() -> string
			{
				auto moveError = oldPostAction();
				if (!moveError.empty())
				{
					return moveError;
				}

				try
				{
					cachefiles::getReleaseInfo(*_config, targetPath);
				}
				catch (Exception& e)
				{
					return e.what();
				}
				return string(); // success
			};
			downloadEntity.postAction = extendedPostAction;
		}

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
	auto signatureLongAlias = indexEntry.uri + ' ' + signatureAlias;
	_logger->log(Logger::Subsystem::Metadata, 3,
			__get_download_log_message(signatureLongAlias));

	auto signaturePostAction = generateMovingSub(signatureDownloadPath, signatureTargetPath);

	if (!simulating and runChecks)
	{
		auto oldSignaturePostAction = signaturePostAction;
		signaturePostAction = [oldSignaturePostAction, longAlias, targetPath, signatureTargetPath, &_config]() -> string
		{
			auto moveError = oldSignaturePostAction();
			if (!moveError.empty())
			{
				return moveError;
			}

			if (!cachefiles::verifySignature(*_config, targetPath))
			{
				warn("signature verification for '%s' failed", longAlias.c_str());

				if (!_config->getBool("cupt::update::keep-bad-signatures"))
				{
					// for compatibility with APT tools delete the downloaded file
					if (unlink(signatureTargetPath.c_str()) == -1)
					{
						warn("unable to delete file '%s': EEE", signatureTargetPath.c_str());
					}
				}
			}
			return string();
		};
	}

	{
		download::Manager::DownloadEntity downloadEntity;

		download::Manager::ExtendedUri extendedUri(download::Uri(signatureUri),
				signatureAlias, signatureLongAlias);

		downloadEntity.extendedUris.push_back(std::move(extendedUri));
		downloadEntity.targetPath = signatureDownloadPath;
		downloadEntity.postAction = signaturePostAction;
		downloadEntity.size = (size_t)-1;

		downloadManager.download(
				vector< download::Manager::DownloadEntity >{ downloadEntity });
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

string getBaseUri(const string& uri)
{
	auto slashPosition = uri.rfind('/');
	return uri.substr(0, slashPosition);
}

// all this function is just guesses, there are no documentation
bool __download_and_apply_patches(download::Manager& downloadManager,
		const Cache::IndexDownloadRecord& downloadRecord,
		const Cache::IndexEntry& indexEntry, const string& baseDownloadPath,
		const string& diffIndexPath, const string& targetPath,
		Logger* logger)
{
	// total hash -> { name of the patch-to-apply, total size }
	map< string, pair< string, size_t > > history;
	// name -> { hash, size }
	map< string, pair< string, size_t > > patches;
	string wantedHashSum;

	auto baseUri = getBaseUri(downloadRecord.uri);

	auto baseAlias = indexEntry.distribution + '/' + indexEntry.component +
			' ' + getUriBasename(baseUri);
	auto baseLongAlias = indexEntry.uri + ' ' + baseAlias;

	auto patchedPath = baseDownloadPath + ".patched";

	auto cleanUp = [patchedPath, diffIndexPath]()
	{
		unlink(patchedPath.c_str());
		unlink(diffIndexPath.c_str());
	};
	auto fail = [baseLongAlias, &cleanUp]()
	{
		cleanUp();
		warn("%s: failed to proceed", baseLongAlias.c_str());
	};

	try
	{
		{ // parsing diff index
			string openError;
			File diffIndexFile(diffIndexPath, "r", openError);
			if (!openError.empty())
			{
				fatal("unable to open the file '%s': EEE", diffIndexPath.c_str());
			}

			TagParser diffIndexParser(&diffIndexFile);
			TagParser::StringRange fieldName, fieldValue;
			smatch m;
			while (diffIndexParser.parseNextLine(fieldName, fieldValue))
			{
				bool isHistory = fieldName.equal(BUFFER_AND_SIZE("SHA1-History"));
				bool isPatchInfo = fieldName.equal(BUFFER_AND_SIZE("SHA1-Patches"));
				if (isHistory || isPatchInfo)
				{
					string block;
					diffIndexParser.parseAdditionalLines(block);
					// TODO: make it common?
					static const sregex checksumsLineRegex = sregex::compile(
							" ([[:xdigit:]]+) +(\\d+) +(.*)", regex_constants::optimize);

					auto lines = internal::split('\n', block);
					FORIT(lineIt, lines)
					{
						const string& line = *lineIt;
						if (!regex_match(line, m, checksumsLineRegex))
						{
							fatal("malformed 'hash-size-name' line '%s'", line.c_str());
						}

						if (isHistory)
						{
							history[m[1]] = make_pair(string(m[3]), string2uint32(m[2]));
						}
						else // patches
						{
							patches[m[3]] = make_pair(string(m[1]), string2uint32(m[2]));
						}
					}
				}
				else if (fieldName.equal(BUFFER_AND_SIZE("SHA1-Current")))
				{
					auto values = internal::split(' ', string(fieldValue));
					if (values.size() != 2)
					{
						fatal("malformed 'hash-size' line '%s'", string(fieldValue).c_str());
					}
					wantedHashSum = values[0];
				}
			}
			if (wantedHashSum.empty())
			{
				fatal("failed to find wanted hash sum");
			}
		}
		if (unlink(diffIndexPath.c_str()) == -1)
		{
			warn("unable to delete a temporary index file '%s'", diffIndexPath.c_str());
		}

		HashSums subTargetHashSums;
		subTargetHashSums.fill(targetPath);
		const string& currentSha1Sum = subTargetHashSums[HashSums::SHA1];

		const string initialSha1Sum = currentSha1Sum;

		if (::system(sf("cp %s %s", targetPath.c_str(), patchedPath.c_str()).c_str()))
		{
			fatal("unable to copy '%s' to '%s'", targetPath.c_str(), patchedPath.c_str());
		}

		while (currentSha1Sum != wantedHashSum)
		{
			auto historyIt = history.find(currentSha1Sum);
			if (historyIt == history.end())
			{
				if (currentSha1Sum == initialSha1Sum)
				{
					logger->log(Logger::Subsystem::Metadata, 3, __get_pidded_string(
							"no matching index patches found, presumably the local index is too old"));
					cleanUp();
					return false; // local index is too old
				}
				else
				{
					fatal("unable to find a patch for the sha1 sum '%s'", currentSha1Sum.c_str());
				}
			}

			const string& patchName = historyIt->second.first;
			auto patchIt = patches.find(patchName);
			if (patchIt == patches.end())
			{
				fatal("unable to a patch entry for the patch '%s'", patchName.c_str());
			}

			string patchSuffix = "/" + patchName + ".gz";
			auto alias = baseAlias + patchSuffix;
			auto longAlias = baseLongAlias + patchSuffix;
			logger->log(Logger::Subsystem::Metadata, 3, __get_download_log_message(longAlias));

			auto unpackedPath = baseDownloadPath + '.' + patchName;
			auto downloadPath = unpackedPath + ".gz";

			download::Manager::DownloadEntity downloadEntity;

			auto patchUri = baseUri + patchSuffix;
			download::Manager::ExtendedUri extendedUri(download::Uri(patchUri),
					alias, longAlias);
			downloadEntity.extendedUris.push_back(std::move(extendedUri));
			downloadEntity.targetPath = downloadPath;
			downloadEntity.size = (size_t)-1;

			HashSums patchHashSums;
			patchHashSums[HashSums::SHA1] = patchIt->second.first;

			std::function< string () > uncompressingSub;
			generateUncompressingSub(patchUri, downloadPath, unpackedPath, uncompressingSub);

			downloadEntity.postAction = [&patchHashSums, &subTargetHashSums,
					&uncompressingSub, &patchedPath, &unpackedPath]() -> string
			{
				auto partialDirectory = fs::dirname(patchedPath);
				auto patchedPathBasename = fs::filename(patchedPath);

				string result = uncompressingSub();
				if (!result.empty())
				{
					return result; // unpackedPath is not yet created
				}

				if (!patchHashSums.verify(unpackedPath))
				{
					result = __("hash sums mismatch");
					goto out;
				}
				if (::system(sf("(cat %s && echo w) | (cd %s && red -s - %s >/dev/null)",
							unpackedPath.c_str(), partialDirectory.c_str(), patchedPathBasename.c_str()).c_str()))
				{
					result = __("applying ed script failed");
					goto out;
				}
				subTargetHashSums.fill(patchedPath);
			 out:
				if (unlink(unpackedPath.c_str()) == -1)
				{
					warn("unable to remove partial index patch file '%s': EEE", unpackedPath.c_str());
				}
				return result;
			};
			auto downloadError = downloadManager.download(
					vector< download::Manager::DownloadEntity >{ downloadEntity });
			if (!downloadError.empty())
			{
				fail();
				return false;
			}
		}

		auto moveError = fs::move(patchedPath, targetPath);
		if (!moveError.empty())
		{
			fatal("%s", moveError.c_str());
		}
		return true;
	}
	catch (...)
	{
		fail();
		return false;
	}
}

bool MetadataWorker::__download_index(download::Manager& downloadManager,
		const Cache::IndexDownloadRecord& downloadRecord, bool isDiff,
		const Cache::IndexEntry& indexEntry, const string& baseDownloadPath,
		const string& targetPath, bool releaseFileChanged, bool simulating)
{
	if (isDiff && !fs::fileExists(targetPath))
	{
		return false; // nothing to patch
	}

	const string& uri = downloadRecord.uri;
	auto downloadPath = baseDownloadPath +
			(isDiff ? string(".diffindex") : getFilenameExtension(uri));

	std::function< string () > uncompressingSub;
	if (isDiff)
	{
		uncompressingSub = []() -> string { return ""; }; // diffIndex is not a final file
	}
	else if (!generateUncompressingSub(uri, downloadPath, targetPath, uncompressingSub))
	{
		return false;
	}

	auto alias = indexEntry.distribution + '/' + indexEntry.component +
			' ' + getUriBasename(uri, isDiff);
	auto longAlias = indexEntry.uri + ' ' + alias;
	_logger->log(Logger::Subsystem::Metadata, 3, __get_download_log_message(longAlias));

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
			alias, longAlias);
	downloadEntity.extendedUris.push_back(std::move(extendedUri));
	downloadEntity.targetPath = downloadPath;
	downloadEntity.size = downloadRecord.size;

	auto hashSums = downloadRecord.hashSums;
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
	if (isDiff && !simulating && downloadError.empty())
	{
		return __download_and_apply_patches(downloadManager, downloadRecord,
				indexEntry, baseDownloadPath, downloadPath, targetPath, _logger);
	}
	else
	{
		return downloadError.empty();
	}
}

bool MetadataWorker::__update_index(download::Manager& downloadManager,
		const Cache::IndexEntry& indexEntry, bool releaseFileChanged, bool& indexFileChanged)
{
	// downloading Packages/Sources
	auto targetPath = cachefiles::getPathOfIndexList(*_config, indexEntry);

	auto downloadInfo = cachefiles::getDownloadInfoOfIndexList(*_config, indexEntry);

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

	auto baseDownloadPath = getDownloadPath(targetPath);

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

	bool useIndexDiffs = _config->getBool("cupt::update::use-index-diffs");
	if (useIndexDiffs && ::system("which red >/dev/null 2>/dev/null"))
	{
		_logger->log(Logger::Subsystem::Metadata, 3,
				__get_pidded_string("the 'red' binary is not available, skipping index diffs"));
		useIndexDiffs = false;
	}

	const string diffIndexSuffix = ".diff/Index";
	auto diffIndexSuffixSize = diffIndexSuffix.size();
	FORIT(downloadRecordIt, downloadInfo)
	{
		const string& uri = downloadRecordIt->uri;
		bool isDiff = (uri.size() >= diffIndexSuffixSize &&
				!uri.compare(uri.size() - diffIndexSuffixSize, diffIndexSuffixSize, diffIndexSuffix));
		if (isDiff && !useIndexDiffs)
		{
			continue;
		}

		if(__download_index(downloadManager, *downloadRecordIt, isDiff, indexEntry,
				baseDownloadPath, targetPath, releaseFileChanged, simulating))
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
	auto downloadInfo = cachefiles::getDownloadInfoOfLocalizedDescriptions(*_config, indexEntry);
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
				' ' + getUriBasename(uri, true);
		auto longAlias = indexEntry.uri + ' ' + alias;
		_logger->log(Logger::Subsystem::Metadata, 3, __get_download_log_message(longAlias));

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
				alias, longAlias);
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
	auto indexEntryDescription =
			string(indexEntry.category == Cache::IndexEntry::Binary ? "deb" : "deb-src") +
			' ' + indexEntry.uri + ' ' + indexEntry.distribution + '/' + indexEntry.component;
	_logger->log(Logger::Subsystem::Metadata, 2,
			__get_pidded_string(string("updating: ") + indexEntryDescription));

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

void MetadataWorker::__list_cleanup(const string& lockPath)
{
	_logger->log(Logger::Subsystem::Metadata, 2, "cleaning up old index lists");

	set< string > usedPaths;
	auto addUsedPrefix = [&usedPaths](const string& prefix)
	{
		auto existingFiles = fs::glob(prefix + '*');
		FORIT(existingFileIt, existingFiles)
		{
			usedPaths.insert(*existingFileIt);
		}
	};

	auto indexEntries = _cache->getIndexEntries();
	FORIT(indexEntryIt, indexEntries)
	{
		addUsedPrefix(cachefiles::getPathOfReleaseList(*_config, *indexEntryIt));
		addUsedPrefix(cachefiles::getPathOfIndexList(*_config, *indexEntryIt));

		auto translationsDownloadInfo =
				cachefiles::getDownloadInfoOfLocalizedDescriptions(*_config, *indexEntryIt);
		FORIT(downloadRecordIt, translationsDownloadInfo)
		{
			addUsedPrefix(downloadRecordIt->localPath);
		}
	}
	addUsedPrefix(lockPath);

	bool simulating = _config->getBool("cupt::worker::simulate");

	auto allListFiles = fs::glob(__get_indexes_directory() + "/*");
	FORIT(fileIt, allListFiles)
	{
		if (!usedPaths.count(*fileIt) && fs::fileExists(*fileIt) /* is a file */)
		{
			// needs deletion
			if (simulating)
			{
				simulate("deleting '%s'", fileIt->c_str());
			}
			else
			{
				if (unlink(fileIt->c_str()) == -1)
				{
					warn("unable to delete '%s': EEE", fileIt->c_str());
				}
			}
		}
	}
}

void MetadataWorker::updateReleaseAndIndexData(const shared_ptr< download::Progress >& downloadProgress)
{
	_logger->log(Logger::Subsystem::Metadata, 1, "updating package metadata");

	auto indexesDirectory = __get_indexes_directory();
	bool simulating = _config->getBool("cupt::worker::simulate");
	if (!simulating)
	{
		if (!fs::dirExists(indexesDirectory))
		{
			if (mkdir(indexesDirectory.c_str(), 0755) == -1)
			{
				fatal("unable to create the lists directory '%s': EEE", indexesDirectory.c_str());
			}
		}
	}

	shared_ptr< internal::Lock > lock;
	string lockFilePath = indexesDirectory + "/lock";
	if (!simulating)
	{
		lock.reset(new internal::Lock(_config, lockFilePath));
	}

	{ // run pre-actions
		_logger->log(Logger::Subsystem::Metadata, 2, "running apt pre-invoke hooks");
		auto preCommands = _config->getList("apt::update::pre-invoke");
		FORIT(commandIt, preCommands)
		{
			auto errorId = sf("pre-invoke action '%s'", commandIt->c_str());
			_run_external_command(Logger::Subsystem::Metadata, *commandIt, "", errorId);
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

	if (_config->getBool("apt::get::list-cleanup"))
	{
		__list_cleanup(lockFilePath);
	}

	{ // run post-actions
		_logger->log(Logger::Subsystem::Metadata, 2, "running apt post-invoke hooks");
		auto postCommands = _config->getList("apt::update::post-invoke");
		FORIT(commandIt, postCommands)
		{
			auto errorId = sf("post-invoke action '%s'", commandIt->c_str());
			_run_external_command(Logger::Subsystem::Metadata, *commandIt, "", errorId);
		}
		if (masterExitCode)
		{
			_logger->log(Logger::Subsystem::Metadata, 2, "running apt post-invoke-success hooks");
			auto postSuccessCommands = _config->getList("apt::update::post-invoke-success");
			FORIT(commandIt, postSuccessCommands)
			{
				auto errorId = sf("post-invoke-success action '%s'", commandIt->c_str());
				_run_external_command(Logger::Subsystem::Metadata, *commandIt, "", errorId);
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

