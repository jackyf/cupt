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
#include <algorithm>
#include <queue>

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
#include <internal/exceptionlessfuture.hpp>

#include <internal/worker/metadata.hpp>
#include <internal/worker/temppath.hpp>

namespace cupt {
namespace internal {

enum class MetadataWorker::IndexType { Packages, PackagesDiff, Translation, TranslationDiff };

bool MetadataWorker::__is_diff_type(const IndexType& indexType)
{
	return indexType == IndexType::PackagesDiff || indexType == IndexType::TranslationDiff;
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

std::function< string () > generateMovingSub(const SharedTempPath& downloadPath, const string& targetPath)
{
	return [downloadPath=downloadPath, targetPath]() mutable -> string
	{
		ioi::removeIndexOfIndex(targetPath);
		if (fs::move(downloadPath, targetPath))
		{
			downloadPath.abandon();
			return "";
		}
		else
		{
			return format2e(__("unable to rename '%s' to '%s'"), (string)downloadPath, targetPath);
		}
	};
};

bool generateUncompressingSub(const download::Uri& uri, const SharedTempPath& downloadPath,
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
			SharedTempPath uncompressedPath { (string)downloadPath + ".uncompressed" };
			auto uncompressingResult = ::system(format2("%s %s -c > %s",
					uncompressorName, (string)downloadPath, string(uncompressedPath)).c_str());
			if (uncompressingResult)
			{
				return format2(__("failed to uncompress '%s', '%s' returned the error %d"),
						(string)downloadPath, uncompressorName, uncompressingResult);
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
	return format2("(%x): %s", (unsigned)pthread_self(), input);
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

std::function< string() > combineDownloadPostActions(
		const std::function< string () >& left,
		const std::function< string () >& right)
{
	return [left, right]()
	{
		auto value = left();
		if (value.empty())
		{
			value = right();
		}
		return value;
	};
}

std::function< string() > getReleaseCheckPostAction(const Config&, const string& path, const string&)
{
	return [path]() -> string
	{
		try
		{
			cachefiles::getReleaseInfo(path, path);
		}
		catch (Exception& e)
		{
			return e.what();
		}
		return string(); // success
	};
}

string deDotGpg(const string& input)
{
	if (getFilenameExtension(input) == ".gpg")
	{
		return input.substr(0, input.size() - 4);
	}
	else
	{
		return input;
	}
}

std::function< string() > getReleaseSignatureCheckPostAction(
		const Config& config, const string& path, const string& alias)
{
	return [&config, path, alias]() -> string
	{
		cachefiles::verifySignature(config, deDotGpg(path), deDotGpg(alias));
		return string();
	};
}

bool MetadataWorker::__downloadReleaseLikeFile(download::Manager& downloadManager,
		const string& uri, const string& targetPath,
		const cachefiles::IndexEntry& indexEntry, const string& name,
		SecondPostActionGeneratorForReleaseLike secondPostActionGenerator)
{
	bool runChecks = _config->getBool("cupt::update::check-release-files");

	auto alias = indexEntry.distribution + ' ' + name;
	auto longAlias = indexEntry.uri + ' ' + alias;
	SharedTempPath downloadPath { getDownloadPath(targetPath) };

	_logger->log(Logger::Subsystem::Metadata, 3, __get_download_log_message(longAlias));
	{
		download::Manager::DownloadEntity downloadEntity;

		downloadEntity.extendedUris.emplace_back(download::Uri(uri), alias, longAlias);
		downloadEntity.targetPath = downloadPath;
		downloadEntity.postAction = generateMovingSub(downloadPath, targetPath);
		downloadEntity.size = (size_t)-1;
		downloadEntity.optional = true;

		if (runChecks)
		{
			downloadEntity.postAction = combineDownloadPostActions(downloadEntity.postAction,
					secondPostActionGenerator(*_config, targetPath, longAlias));
		}

		return downloadManager.download({ downloadEntity }).empty();
	}
}

HashSums fillHashSumsIfPresent(const string& path)
{
	HashSums hashSums; // empty now
	hashSums[HashSums::MD5] = "0"; // won't match for sure
	if (fs::fileExists(path))
	{
		// the Release file already present
		hashSums.fill(path);
	}
	return hashSums;
}

bool MetadataWorker::__downloadRelease(download::Manager& downloadManager,
		const cachefiles::IndexEntry& indexEntry, bool& releaseFileChanged)
{
	const auto simulating = _config->getBool("cupt::worker::simulate");

	auto uri = cachefiles::getDownloadUriOfReleaseList(indexEntry);
	auto targetPath = cachefiles::getPathOfReleaseList(*_config, indexEntry);

	auto hashSums = fillHashSumsIfPresent(targetPath);
	releaseFileChanged = false;

	if (!__downloadReleaseLikeFile(downloadManager, uri, targetPath, indexEntry, "Release", getReleaseCheckPostAction))
	{
		return false;
	}

	releaseFileChanged = simulating || !hashSums.verify(targetPath);

	__downloadReleaseLikeFile(downloadManager, uri+".gpg", targetPath+".gpg", indexEntry, "Release.gpg", getReleaseSignatureCheckPostAction);

	return true;
}

// InRelease == inside signed Release
bool MetadataWorker::__downloadInRelease(download::Manager& downloadManager,
		const cachefiles::IndexEntry& indexEntry, bool& releaseFileChanged)
{
	const auto simulating = _config->getBool("cupt::worker::simulate");

	auto uri = cachefiles::getDownloadUriOfInReleaseList(indexEntry);
	auto targetPath = cachefiles::getPathOfInReleaseList(*_config, indexEntry);

	auto hashSums = fillHashSumsIfPresent(targetPath);
	releaseFileChanged = false;

	bool downloadResult = __downloadReleaseLikeFile(downloadManager, uri, targetPath, indexEntry,
			"InRelease", getReleaseSignatureCheckPostAction);
	releaseFileChanged = downloadResult && (simulating || !hashSums.verify(targetPath));

	return downloadResult;
}

bool MetadataWorker::__update_release(download::Manager& downloadManager,
		const cachefiles::IndexEntry& indexEntry, bool& releaseFileChanged)
{
	bool result = __downloadInRelease(downloadManager, indexEntry, releaseFileChanged) ||
			__downloadRelease(downloadManager, indexEntry, releaseFileChanged);
	if (!result)
	{
		warn2(__("failed to download %s for '%s %s/%s'"), "(In)Release", indexEntry.uri, indexEntry.distribution, "");
	}
	return result;
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
		const string& diffIndexPath_, const string& targetPath,
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

	try
	{
		{ // parsing diff index
			SharedTempPath diffIndexPath(diffIndexPath_);

			string openError;
			File diffIndexFile(diffIndexPath, "r", openError);
			if (!openError.empty())
			{
				logger->loggedFatal2(Logger::Subsystem::Metadata, 3,
						piddedFormat2e, "unable to open the file '%s': %s", string(diffIndexPath), openError);
			}

			TagParser diffIndexParser(&diffIndexFile);
			TagParser::StringRange fieldName, fieldValue;
			smatch m;
			while (diffIndexParser.parseNextLine(fieldName, fieldValue))
			{
				bool isHistory = fieldName.equal(BUFFER_AND_SIZE("SHA1-History"));
				bool isDownloadInfo = fieldName.equal(BUFFER_AND_SIZE("SHA1-Download"));
				if (isHistory || isDownloadInfo)
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
					auto values = internal::split(' ', fieldValue.toString());
					if (values.size() != 2)
					{
						logger->loggedFatal2(Logger::Subsystem::Metadata, 3,
								piddedFormat2, "malformed 'hash-size' line '%s'", fieldValue.toString());
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

		HashSums subTargetHashSums;
		subTargetHashSums.fill(targetPath);
		const string& currentSha1Sum = subTargetHashSums[HashSums::SHA1];

		const string initialSha1Sum = currentSha1Sum;

		SharedTempPath patchedPath { baseDownloadPath + ".patched" };
		if (::system(format2("cp %s %s", targetPath, (string)patchedPath).c_str()))
		{
			logger->loggedFatal2(Logger::Subsystem::Metadata, 3,
					piddedFormat2, "unable to copy '%s' to '%s'", targetPath, (string)patchedPath);
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
					return false; // local index is too old
				}
				else
				{
					logger->loggedFatal2(Logger::Subsystem::Metadata, 3,
							piddedFormat2, "unable to find a patch for the sha1 sum '%s'", currentSha1Sum);
				}
			}

			const string& patchName = historyIt->second.first;
			auto patchFileName = patchName + ".gz";
			auto patchIt = patches.find(patchFileName);
			if (patchIt == patches.end())
			{
				logger->loggedFatal2(Logger::Subsystem::Metadata, 3,
						piddedFormat2, "unable to find a patch entry for the patch '%s'", patchName);
			}

			string patchSuffix = "/" + patchFileName;
			auto alias = baseAlias + patchSuffix;
			auto longAlias = baseLongAlias + patchSuffix;
			logger->log(Logger::Subsystem::Metadata, 3, __get_download_log_message(longAlias));

			SharedTempPath downloadPath { baseDownloadPath + '.' + patchFileName };
			SharedTempPath unpackedPath { baseDownloadPath + '.' + patchName };

			download::Manager::DownloadEntity downloadEntity;

			auto patchUri = baseUri + patchSuffix;
			download::Manager::ExtendedUri extendedUri(download::Uri(patchUri),
					alias, longAlias);
			downloadEntity.extendedUris.push_back(std::move(extendedUri));
			downloadEntity.targetPath = downloadPath;
			downloadEntity.size = patchIt->second.second;

			HashSums patchHashSums;
			patchHashSums[HashSums::SHA1] = patchIt->second.first;

			std::function< string () > uncompressingSub;
			generateUncompressingSub(patchUri, downloadPath, unpackedPath, uncompressingSub);

			downloadEntity.postAction = [&patchHashSums, &downloadPath,
					&uncompressingSub, &patchedPath, &unpackedPath]() -> string
			{
				if (!patchHashSums.verify(downloadPath))
				{
					return __("hash sums mismatch");
				}

				auto partialDirectory = fs::dirname(patchedPath);
				auto patchedPathBasename = fs::filename(patchedPath);

				string result = uncompressingSub();
				if (!result.empty())
				{
					return result;
				}

				if (::system(format2("(cat %s && echo w) | (cd %s && red -s - %s >/dev/null)",
							(string)unpackedPath, partialDirectory, patchedPathBasename).c_str()))
				{
					return __("applying ed script failed");
				}
				return {};
			};
			auto downloadError = downloadManager.download(
					vector< download::Manager::DownloadEntity >{ downloadEntity });
			if (!downloadError.empty())
			{
				throw std::runtime_error(""); // error message is reported already by download manager
			}

			subTargetHashSums.fill(patchedPath);
		}

		if (!fs::move(patchedPath, targetPath))
		{
			logger->loggedFatal2(Logger::Subsystem::Metadata, 3,
					piddedFormat2e, "unable to rename '%s' to '%s'", (string)patchedPath, targetPath);
		}
		return true;
	}
	catch (...)
	{
		warn2(__("%s: failed to proceed"), baseLongAlias);
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
	if (__is_diff_type(indexType))
	{
		uncompressingSub = []() -> string { return ""; }; // is not a final file
	}
	else if (!generateUncompressingSub(uri, SharedTempPath(downloadPath), targetPath, uncompressingSub))
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

bool MetadataWorker::__update_main_index(download::Manager& downloadManager,
		const cachefiles::IndexEntry& indexEntry, bool releaseFileChanged, bool& mainIndexFileChanged)
{
	// downloading Packages/Sources
	IndexUpdateInfo info;
	info.type = IndexType::Packages;
	info.targetPath = cachefiles::getPathOfIndexList(*_config, indexEntry);
	info.downloadInfo = cachefiles::getDownloadInfoOfIndexList(*_config, indexEntry);
	info.label = __("index");
	return __update_index(downloadManager, indexEntry,
			std::move(info), releaseFileChanged, mainIndexFileChanged);
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
			else if (indexType == IndexType::Translation)
			{
				indexType = IndexType::TranslationDiff;
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

void MetadataWorker::__update_translations(download::Manager& downloadManager,
		const cachefiles::IndexEntry& indexEntry, bool indexFileChanged)
{
	auto downloadInfoV3 = cachefiles::getDownloadInfoOfLocalizedDescriptions3(*_config, indexEntry);
	for (const auto& record: downloadInfoV3)
	{
		IndexUpdateInfo info;
		info.type = IndexType::Translation;
		info.label = format2(__("'%s' descriptions localization"), record.language);
		info.targetPath = record.localPath;
		info.downloadInfo = record.fileDownloadRecords;
		bool unused;
		__update_index(downloadManager, indexEntry, std::move(info), indexFileChanged, unused);
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
	for (const auto& indexEntry: _cache->getIndexEntries())
	{
		auto pathOfIndexList = cachefiles::getPathOfIndexList(*_config, indexEntry);

		addUsedPattern(cachefiles::getPathOfReleaseList(*_config, indexEntry) + '*');
		addUsedPattern(cachefiles::getPathOfInReleaseList(*_config, indexEntry));
		addUsedPattern(pathOfIndexList);
		if (includeIoi)
		{
			addUsedPattern(ioi::getIndexOfIndexPath(pathOfIndexList));
		}

		auto translationsPossiblePaths =
				cachefiles::getPathsOfLocalizedDescriptions(*_config, indexEntry);
		FORIT(pathIt, translationsPossiblePaths)
		{
			addUsedPattern(pathIt->second);
			if (includeIoi)
			{
				addUsedPattern(ioi::getIndexOfIndexPath(pathIt->second));
			}
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

void MetadataWorker::p_generateIndexesOfIndexes(const cachefiles::IndexEntry& indexEntry)
{
	if (_config->getBool("cupt::worker::simulate")) return;

	auto getIoiTemporaryPath = [](const string& path)
	{
		return getDownloadPath(path) + ".ioi";
	};
	auto generateForPath = [&getIoiTemporaryPath](const string& path, bool isMainIndex /* or translation one */)
	{
		if (!fs::fileExists(path)) return;
		auto generator = (isMainIndex ? ioi::ps::generate : ioi::tr::generate);
		generator(path, getIoiTemporaryPath(path));
	};

	generateForPath(cachefiles::getPathOfIndexList(*_config, indexEntry), true);
	for (const auto& item: cachefiles::getPathsOfLocalizedDescriptions(*_config, indexEntry))
	{
		generateForPath(item.second, false);
	}
}

bool MetadataWorker::p_metadataUpdateThread(download::Manager& downloadManager, const cachefiles::IndexEntry& indexEntry)
{
	// wrapping all errors here
	try
	{
		bool result = __update_release_and_index_data(downloadManager, indexEntry);
		if (_config->getBool("cupt::update::generate-index-of-index"))
		{
			p_generateIndexesOfIndexes(indexEntry);
		}
		return result;
	}
	catch (...)
	{
		return false;
	}
}

bool MetadataWorker::p_runMetadataUpdateThreads(const shared_ptr< download::Progress >& downloadProgress)
{
	bool result = true;
	{ // download manager involved part
		download::Manager downloadManager(_config, downloadProgress);

		std::queue< ExceptionlessFuture<bool> > threadReturnValues;

		for (const auto& indexEntry: _cache->getIndexEntries())
		{
			threadReturnValues.emplace(
					std::bind(&MetadataWorker::p_metadataUpdateThread, this, std::ref(downloadManager), indexEntry));
		}
		while (!threadReturnValues.empty())
		{
			if (!threadReturnValues.front().get())
			{
				result = false;
			}
			threadReturnValues.pop();
		}
	}
	return result;
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
				_run_external_command(Logger::Subsystem::Metadata, *commandIt, {}, errorId);
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

	auto masterExitCode = p_runMetadataUpdateThreads(downloadProgress);

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
			_run_external_command(Logger::Subsystem::Metadata, *commandIt, {}, errorId);
		}
		if (masterExitCode)
		{
			_logger->log(Logger::Subsystem::Metadata, 2, "running apt post-invoke-success hooks");
			auto postSuccessCommands = _config->getList("apt::update::post-invoke-success");
			FORIT(commandIt, postSuccessCommands)
			{
				auto errorId = format2("post-invoke-success action '%s'", *commandIt);
				_run_external_command(Logger::Subsystem::Metadata, *commandIt, {}, errorId);
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

