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
#include <sys/wait.h>

#include <algorithm>

#include <common/regex.hpp>

#include <cupt/config.hpp>
#include <cupt/download/uri.hpp>
#include <cupt/download/manager.hpp>
#include <cupt/file.hpp>

#include <internal/filesystem.hpp>
#include <internal/lock.hpp>
#include <internal/tagparser.hpp>
#include <internal/common.hpp>
#include <internal/indexofindex.hpp>

#include <internal/worker/metadata.hpp>

namespace cupt {
namespace internal {

enum class MetadataWorker::IndexType { Packages, PackagesDiff, Localization, LocalizationFile, LocalizationFileDiff };

bool MetadataWorker::__is_diff_type(const IndexType& indexType)
{
	return indexType == IndexType::PackagesDiff || indexType == IndexType::LocalizationFileDiff;
}

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
		ioi::removeIndexOfIndex(targetPath);
		if (fs::move(downloadPath, targetPath))
		{
			return "";
		}
		else
		{
			return format2e(__("unable to rename '%s' to '%s'"), downloadPath, targetPath);
		}
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
			fatal2i("extension '%s' has no uncompressor", filenameExtension);
		}

		if (::system(format2("which %s >/dev/null", uncompressorName).c_str()))
		{
			warn2(__("the '%s' uncompressor is not available, not downloading '%s'"),
					uncompressorName, string(uri));
			return false;
		}

		sub = [uncompressorName, downloadPath, targetPath]() -> string
		{
			auto uncompressedPath = downloadPath + ".uncompressed";
			auto uncompressingResult = ::system(format2("%s %s -c > %s",
					uncompressorName, downloadPath, uncompressedPath).c_str());
			// anyway, remove the compressed file, ignoring errors if any
			unlink(downloadPath.c_str());
			if (uncompressingResult)
			{
				return format2(__("failed to uncompress '%s', '%s' returned the error %d"),
						downloadPath, uncompressorName, uncompressingResult);
			}
			return generateMovingSub(uncompressedPath, targetPath)();
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
		warn2(__("unknown file extension '%s', not downloading '%s'"),
					filenameExtension, string(uri));
		return false;
	}
};

string __get_pidded_string(const string& input)
{
	return format2("(%d): %s", getpid(), input);
}
template < typename... Args >
string piddedFormat2(const string& format, const Args&... args)
{
	return __get_pidded_string(format2(format, args...));
}
template < typename... Args >
string piddedFormat2e(const string& format, const Args&... args)
{
	return __get_pidded_string(format2e(format, args...));
}

string __get_download_log_message(const string& longAlias)
{
	return __get_pidded_string(string("downloading: ") + longAlias);
}

bool MetadataWorker::__update_release(download::Manager& downloadManager,
		const cachefiles::IndexEntry& indexEntry, bool& releaseFileChanged)
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
					cachefiles::getReleaseInfo(*_config, targetPath, targetPath);
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

			if (!cachefiles::verifySignature(*_config, targetPath, longAlias))
			{
				if (!_config->getBool("cupt::update::keep-bad-signatures"))
				{
					// for compatibility with APT tools delete the downloaded file
					if (unlink(signatureTargetPath.c_str()) == -1)
					{
						warn2e(__("unable to remove the file '%s'"), signatureTargetPath);
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

// TODO: make it common?
static const sregex checksumsLineRegex = sregex::compile(
		" ([[:xdigit:]]+) +(\\d+) +(.*)", regex_constants::optimize);

// all this function is just guesses, there are no documentation
bool __download_and_apply_patches(download::Manager& downloadManager,
		const cachefiles::FileDownloadRecord& downloadRecord,
		const cachefiles::IndexEntry& indexEntry, const string& baseDownloadPath,
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
		warn2(__("%s: failed to proceed"), baseLongAlias);
	};

	try
	{
		{ // parsing diff index
			string openError;
			File diffIndexFile(diffIndexPath, "r", openError);
			if (!openError.empty())
			{
				logger->loggedFatal2(Logger::Subsystem::Metadata, 3,
						piddedFormat2e, "unable to open the file '%s': %s", diffIndexPath, openError);
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

					auto lines = internal::split('\n', block);
					FORIT(lineIt, lines)
					{
						const string& line = *lineIt;
						if (!regex_match(line, m, checksumsLineRegex))
						{
							logger->loggedFatal2(Logger::Subsystem::Metadata, 3,
									piddedFormat2, "malformed 'hash-size-name' line '%s'", line);
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
						logger->loggedFatal2(Logger::Subsystem::Metadata, 3,
								piddedFormat2, "malformed 'hash-size' line '%s'", string(fieldValue));
					}
					wantedHashSum = values[0];
				}
			}
			if (wantedHashSum.empty())
			{
				logger->loggedFatal2(Logger::Subsystem::Metadata, 3,
						piddedFormat2, "failed to find the target hash sum");
			}
		}
		if (unlink(diffIndexPath.c_str()) == -1)
		{
			warn2(__("unable to remove the file '%s'"), diffIndexPath);
		}

		HashSums subTargetHashSums;
		subTargetHashSums.fill(targetPath);
		const string& currentSha1Sum = subTargetHashSums[HashSums::SHA1];

		const string initialSha1Sum = currentSha1Sum;

		if (::system(format2("cp %s %s", targetPath, patchedPath).c_str()))
		{
			logger->loggedFatal2(Logger::Subsystem::Metadata, 3,
					piddedFormat2, "unable to copy '%s' to '%s'", targetPath, patchedPath);
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
					logger->loggedFatal2(Logger::Subsystem::Metadata, 3,
							piddedFormat2, "unable to find a patch for the sha1 sum '%s'", currentSha1Sum);
				}
			}

			const string& patchName = historyIt->second.first;
			auto patchIt = patches.find(patchName);
			if (patchIt == patches.end())
			{
				logger->loggedFatal2(Logger::Subsystem::Metadata, 3,
						piddedFormat2, "unable to find a patch entry for the patch '%s'", patchName);
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
				if (::system(format2("(cat %s && echo w) | (cd %s && red -s - %s >/dev/null)",
							unpackedPath, partialDirectory, patchedPathBasename).c_str()))
				{
					result = __("applying ed script failed");
					goto out;
				}
				subTargetHashSums.fill(patchedPath);
			 out:
				if (unlink(unpackedPath.c_str()) == -1)
				{
					warn2e(__("unable to remove the file '%s'"), unpackedPath);
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

		if (!fs::move(patchedPath, targetPath))
		{
			logger->loggedFatal2(Logger::Subsystem::Metadata, 3,
					piddedFormat2e, "unable to rename '%s' to '%s'", patchedPath, targetPath);
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
		const cachefiles::FileDownloadRecord& downloadRecord, IndexType indexType,
		const cachefiles::IndexEntry& indexEntry, const string& baseDownloadPath,
		const string& targetPath, bool sourceFileChanged)
{
	bool simulating = _config->getBool("cupt::worker::simulate");
	if (__is_diff_type(indexType) && !fs::fileExists(targetPath))
	{
		return false; // nothing to patch
	}

	const string& uri = downloadRecord.uri;
	auto downloadPath = baseDownloadPath;
	if (__is_diff_type(indexType))
	{
		downloadPath += ".diffindex";
	}
	else
	{
		downloadPath += getFilenameExtension(uri);
	}

	std::function< string () > uncompressingSub;
	if (__is_diff_type(indexType) || indexType == IndexType::Localization)
	{
		uncompressingSub = []() -> string { return ""; }; // is not a final file
	}
	else if (!generateUncompressingSub(uri, downloadPath, targetPath, uncompressingSub))
	{
		return false;
	}

	auto alias = indexEntry.distribution + '/' + indexEntry.component +
			' ' + getUriBasename(uri, indexType != IndexType::Packages);
	auto longAlias = indexEntry.uri + ' ' + alias;
	_logger->log(Logger::Subsystem::Metadata, 3, __get_download_log_message(longAlias));

	if (!simulating)
	{
		// here we check for outdated dangling indexes in partial directory
		if (sourceFileChanged && fs::fileExists(downloadPath))
		{
			if (unlink(downloadPath.c_str()) == -1)
			{
				warn2e(__("unable to remove an outdated partial file '%s'"), downloadPath);
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
				warn2e(__("unable to remove the file '%s'"), downloadPath);
			}
			return __("hash sums mismatch");
		}
		return uncompressingSub();
	};
	auto downloadError = downloadManager.download(
			vector< download::Manager::DownloadEntity >{ downloadEntity });
	if (indexType != IndexType::Packages && !simulating && downloadError.empty())
	{
		if (__is_diff_type(indexType))
		{
			return __download_and_apply_patches(downloadManager, downloadRecord,
					indexEntry, baseDownloadPath, downloadPath, targetPath, _logger);
		}
		else if (indexType == IndexType::Localization)
		{
			return __download_translations(downloadManager, indexEntry,
					uri, downloadPath, longAlias, sourceFileChanged, _logger);
		}
		return true;
	}
	else
	{
		return downloadError.empty();
	}
}


struct MetadataWorker::IndexUpdateInfo
{
	IndexType type;
	string targetPath;
	vector< cachefiles::FileDownloadRecord > downloadInfo;
	string label;
};

void MetadataWorker::__generate_index_of_index(const string& sourcePath)
{
	if (!_config->getBool("cupt::worker::simulate"))
	{
		auto temporaryPath = getDownloadPath(sourcePath) + ".ioi";
		ioi::generate(sourcePath, temporaryPath);
	}
}

bool MetadataWorker::__update_main_index(download::Manager& downloadManager,
		const cachefiles::IndexEntry& indexEntry, bool releaseFileChanged, bool& mainIndexFileChanged)
{
	// downloading Packages/Sources
	IndexUpdateInfo info;
	info.type = IndexType::Packages;
	info.targetPath = cachefiles::getPathOfIndexList(*_config, indexEntry);
	info.downloadInfo = cachefiles::getDownloadInfoOfIndexList(*_config, indexEntry);
	info.label = __("index");
	auto result = __update_index(downloadManager, indexEntry,
			std::move(info), releaseFileChanged, mainIndexFileChanged);
	if (result && _config->getBool("cupt::update::generate-index-of-index"))
	{
		__generate_index_of_index(info.targetPath);
	}
	return result;
}

bool MetadataWorker::__update_index(download::Manager& downloadManager, const cachefiles::IndexEntry& indexEntry,
		IndexUpdateInfo&& info, bool sourceFileChanged, bool& thisFileChanged)
{
	thisFileChanged = true;

	// checking maybe there is no difference between the local and the remote?
	bool simulating = _config->getBool("cupt::worker::simulate");
	if (!simulating && fs::fileExists(info.targetPath))
	{
		FORIT(downloadRecordIt, info.downloadInfo)
		{
			if (downloadRecordIt->hashSums.verify(info.targetPath))
			{
				// yeah, really
				thisFileChanged = false;
				return true;
			}
		}
	}

	auto baseDownloadPath = getDownloadPath(info.targetPath);

	{ // sort download files by priority and size
		auto comparator = [this](const cachefiles::FileDownloadRecord& left, const cachefiles::FileDownloadRecord& right)
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
		std::sort(info.downloadInfo.begin(), info.downloadInfo.end(), comparator);
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
	FORIT(downloadRecordIt, info.downloadInfo)
	{
		const string& uri = downloadRecordIt->uri;
		bool isDiff = (uri.size() >= diffIndexSuffixSize &&
				!uri.compare(uri.size() - diffIndexSuffixSize, diffIndexSuffixSize, diffIndexSuffix));
		auto indexType = info.type;
		if (isDiff)
		{
			if (!useIndexDiffs)
			{
				continue;
			}
			if (indexType == IndexType::Packages)
			{
				indexType = IndexType::PackagesDiff;
			}
			else if (indexType == IndexType::LocalizationFile)
			{
				indexType = IndexType::LocalizationFileDiff;
			}
			else
			{
				continue; // unknown diff type
			}
		}

		if(__download_index(downloadManager, *downloadRecordIt, indexType, indexEntry,
				baseDownloadPath, info.targetPath, sourceFileChanged))
		{
			return true;
		}
	}

	// we reached here if neither download URI succeeded
	warn2(__("failed to download %s for '%s/%s'"),
			info.label, indexEntry.distribution, indexEntry.component);
	return false;
}

bool MetadataWorker::__download_translations(download::Manager& downloadManager,
		const cachefiles::IndexEntry& indexEntry, const string& localizationIndexUri,
		const string& localizationIndexPath, const string& localizationIndexLongAlias,
		bool sourceFileChanged, Logger* logger)
{
	auto downloadInfo = cachefiles::getDownloadInfoOfLocalizedDescriptions2(*_config, indexEntry);

	map< string, cachefiles::FileDownloadRecord > availableLocalizations;

	{ // parsing localization index
		auto cleanUp = [localizationIndexPath]()
		{
			unlink(localizationIndexPath.c_str());
		};
		auto fail = [localizationIndexLongAlias, &cleanUp]()
		{
			cleanUp();
			warn2(__("failed to parse localization data from '%s'"), localizationIndexLongAlias);
		};

		try
		{
			string openError;
			File localizationIndexFile(localizationIndexPath, "r", openError);
			if (!openError.empty())
			{
				logger->loggedFatal2(Logger::Subsystem::Metadata, 3,
						piddedFormat2e, "unable to open the file '%s': %s", localizationIndexPath, openError);
			}

			TagParser localizationIndexParser(&localizationIndexFile);
			TagParser::StringRange fieldName, fieldValue;
			smatch m;
			while (localizationIndexParser.parseNextLine(fieldName, fieldValue))
			{
				if (fieldName.equal(BUFFER_AND_SIZE("SHA1")))
				{
					string block;
					localizationIndexParser.parseAdditionalLines(block);
					auto lines = internal::split('\n', block);
					FORIT(lineIt, lines)
					{
						const string& line = *lineIt;
						if (!regex_match(line, m, checksumsLineRegex))
						{
							logger->loggedFatal2(Logger::Subsystem::Metadata, 3,
									piddedFormat2, "malformed 'hash-size-name' line '%s'", line);
						}

						cachefiles::FileDownloadRecord record;
						record.uri = getBaseUri(localizationIndexUri) + '/' + m[3];
						record.size = string2uint32(m[2]);
						record.hashSums[HashSums::SHA1] = m[1];

						string searchKey = m[3];
						auto filenameExtension = getFilenameExtension(searchKey);
						if (!filenameExtension.empty())
						{
							searchKey.erase(searchKey.size() - filenameExtension.size());
						}

						availableLocalizations[searchKey] = record;
					}
				}
			}
		}
		catch (...)
		{
			fail();
			return false;
		}
		cleanUp();
	}

	bool result = true;
	FORIT(downloadRecordIt, downloadInfo)
	{
		auto it = availableLocalizations.find(downloadRecordIt->filePart);
		if (it == availableLocalizations.end())
		{
			continue; // not found
		}

		const string& targetPath = downloadRecordIt->localPath;
		if (!__download_index(downloadManager, it->second, IndexType::LocalizationFile, indexEntry,
				getDownloadPath(targetPath), targetPath, sourceFileChanged))
		{
			result = false;
		}
	}

	return result;
}

void MetadataWorker::__update_translations(download::Manager& downloadManager,
		const cachefiles::IndexEntry& indexEntry, bool indexFileChanged)
{
	auto downloadInfoV3 = cachefiles::getDownloadInfoOfLocalizedDescriptions3(*_config, indexEntry);
	if (!downloadInfoV3.empty()) // full info is available directly in Release file
	{
		for (const auto& record: downloadInfoV3)
		{
			IndexUpdateInfo info;
			info.type = IndexType::LocalizationFile;
			info.label = format2(__("'%s' descriptions localization"), record.language);
			info.targetPath = record.localPath;
			info.downloadInfo = record.fileDownloadRecords;
			bool unused;
			__update_index(downloadManager, indexEntry, std::move(info), indexFileChanged, unused);
		}
		return;
	}

	if (cachefiles::getDownloadInfoOfLocalizedDescriptions2(*_config, indexEntry).empty())
	{
		return;
	}

	{ // downloading translation index
		auto localizationIndexDownloadInfo =
				cachefiles::getDownloadInfoOfLocalizationIndex(*_config, indexEntry);
		if (localizationIndexDownloadInfo.empty())
		{
			_logger->log(Logger::Subsystem::Metadata, 3,
					"no localization file index was found in the release, skipping downloading localization files");
			return;
		}
		if (localizationIndexDownloadInfo.size() > 1)
		{
			_logger->log(Logger::Subsystem::Metadata, 3,
					"more than one localization file index was found in the release, skipping downloading localization files");
			return;
		}

		// downloading file containing localized descriptions
		auto baseDownloadPath = getDownloadPath(cachefiles::getPathOfIndexList(*_config, indexEntry)) + "_l10n_Index";
		__download_index(downloadManager, localizationIndexDownloadInfo[0], IndexType::Localization,
				indexEntry, baseDownloadPath, "" /* unused */, indexFileChanged);
	}
}

bool MetadataWorker::__update_release_and_index_data(download::Manager& downloadManager,
		const cachefiles::IndexEntry& indexEntry)
{
	auto indexEntryDescription =
			string(indexEntry.category == cachefiles::IndexEntry::Binary ? "deb" : "deb-src") +
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
	if (!__update_main_index(downloadManager, indexEntry, releaseFileChanged, indexFileChanged))
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
	auto addUsedPattern = [&usedPaths](const string& pattern)
	{
		for (const string& existingFile: fs::glob(pattern))
		{
			usedPaths.insert(existingFile);
		}
	};

	auto includeIoi = _config->getBool("cupt::update::generate-index-of-index");
	auto indexEntries = _cache->getIndexEntries();
	FORIT(indexEntryIt, indexEntries)
	{
		auto pathOfIndexList = cachefiles::getPathOfIndexList(*_config, *indexEntryIt);

		addUsedPattern(cachefiles::getPathOfReleaseList(*_config, *indexEntryIt) + '*');
		addUsedPattern(pathOfIndexList);
		if (includeIoi)
		{
			addUsedPattern(ioi::getIndexOfIndexPath(pathOfIndexList));
		}

		auto translationsPossiblePaths =
				cachefiles::getPathsOfLocalizedDescriptions(*_config, *indexEntryIt);
		FORIT(pathIt, translationsPossiblePaths)
		{
			addUsedPattern(pathIt->second);
		}
	}
	addUsedPattern(lockPath);

	bool simulating = _config->getBool("cupt::worker::simulate");

	auto allListFiles = fs::lglob(__get_indexes_directory(), "*");
	FORIT(fileIt, allListFiles)
	{
		if (!usedPaths.count(*fileIt) && fs::fileExists(*fileIt) /* is a file */)
		{
			// needs deletion
			if (simulating)
			{
				simulate2("deleting '%s'", *fileIt);
			}
			else
			{
				if (unlink(fileIt->c_str()) == -1)
				{
					warn2e(__("unable to remove the file '%s'"), *fileIt);
				}
			}
		}
	}
}

void MetadataWorker::updateReleaseAndIndexData(const shared_ptr< download::Progress >& downloadProgress)
{
	_logger->log(Logger::Subsystem::Metadata, 1, "updating package metadata");

	auto indexesDirectory = __get_indexes_directory();
	string lockFilePath = indexesDirectory + "/lock";
	shared_ptr< internal::Lock > lock;

	try // preparations
	{
		bool simulating = _config->getBool("cupt::worker::simulate");
		if (!simulating)
		{
			if (!fs::dirExists(indexesDirectory))
			{
				if (mkdir(indexesDirectory.c_str(), 0755) == -1)
				{
					_logger->loggedFatal2(Logger::Subsystem::Metadata, 2,
							format2e, "unable to create the lists directory '%s'", indexesDirectory);
				}
			}
		}

		lock.reset(new internal::Lock(*_config, lockFilePath));

		{ // run pre-actions
			_logger->log(Logger::Subsystem::Metadata, 2, "running apt pre-invoke hooks");
			auto preCommands = _config->getList("apt::update::pre-invoke");
			FORIT(commandIt, preCommands)
			{
				auto errorId = format2("pre-invoke action '%s'", *commandIt);
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
					_logger->loggedFatal2(Logger::Subsystem::Metadata, 2,
							format2e, "unable to create the directory '%s'", partialIndexesDirectory);
				}
			}
		}
	}
	catch (...)
	{
		_logger->loggedFatal2(Logger::Subsystem::Metadata, 1, format2, "aborted downloading release and index data");
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
				_logger->loggedFatal2(Logger::Subsystem::Metadata, 2, format2e, "%s() failed", "fork");
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
				_logger->loggedFatal2(Logger::Subsystem::Metadata, 2, format2e, "%s() failed", "wait");
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
			auto errorId = format2("post-invoke action '%s'", *commandIt);
			_run_external_command(Logger::Subsystem::Metadata, *commandIt, "", errorId);
		}
		if (masterExitCode)
		{
			_logger->log(Logger::Subsystem::Metadata, 2, "running apt post-invoke-success hooks");
			auto postSuccessCommands = _config->getList("apt::update::post-invoke-success");
			FORIT(commandIt, postSuccessCommands)
			{
				auto errorId = format2("post-invoke-success action '%s'", *commandIt);
				_run_external_command(Logger::Subsystem::Metadata, *commandIt, "", errorId);
			}
		}
	}

	lock.reset();

	if (!masterExitCode)
	{
		_logger->loggedFatal2(Logger::Subsystem::Metadata, 1,
				format2, "there were errors while downloading release and index data");
	}
}

}
}

