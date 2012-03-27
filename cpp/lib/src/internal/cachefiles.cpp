/**************************************************************************
*   Copyright (C) 2011 by Eugene V. Lyubimkin                             *
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
#include <clocale>
#include <ctime>

#include <common/regex.hpp>

#include <cupt/config.hpp>
#include <cupt/cache/releaseinfo.hpp>
#include <cupt/file.hpp>

#include <internal/cachefiles.hpp>
#include <internal/common.hpp>
#include <internal/filesystem.hpp>

namespace cupt {
namespace internal {
namespace cachefiles {

static string getPathOfIndexEntry(const Config& config, const IndexEntry& entry)
{
	auto escape = [](const string& input) -> string
	{
		auto result = input;
		FORIT(it, result)
		{
			if (*it == '~' || *it == '/' || *it == ':')
			{
				*it = '_';
			}
		}
		return result;
	};

	string directory = config.getPath("cupt::directory::state::lists");

	string distributionPart = escape(entry.distribution);
	string basePart = escape(entry.uri) + "_";
	if (entry.component.empty())
	{
		// easy source type
		basePart += distributionPart;
	}
	else
	{
		// normal source type
		basePart += (string("dists") + "_" + distributionPart);
	}

	return directory + "/" + basePart;
}

static string getUriOfIndexEntry(const IndexEntry& indexEntry)
{
    if (indexEntry.component.empty())
	{
		// easy source type
		return indexEntry.uri + '/' + indexEntry.distribution;
	}
	else
	{
		// normal source type
		return indexEntry.uri + '/' + "dists" + '/' + indexEntry.distribution;
	}
}

string getPathOfReleaseList(const Config& config, const IndexEntry& entry)
{
	return getPathOfIndexEntry(config, entry) + "_Release";
}

string getDownloadUriOfReleaseList(const IndexEntry& entry)
{
	return getUriOfIndexEntry(entry) + "/Release";
}

static string getIndexListSuffix(const Config& config, const IndexEntry& entry, char delimiter)
{
	if (entry.component.empty())
	{
		// easy source type
		return (entry.category == IndexEntry::Binary ? "Packages" : "Sources");
	}
	else
	{
		// normal source type
		string delimiterString = string(1, delimiter);
		if (entry.category == IndexEntry::Binary)
		{
			return join(delimiterString, vector< string >{
					entry.component, string("binary-") + config.getString("apt::architecture"), "Packages" });
		}
		else
		{
			return join(delimiterString, vector< string >{
					entry.component, "source", "Sources" });
		}
	}
}

string getPathOfIndexList(const Config& config, const IndexEntry& entry)
{
	auto basePath = getPathOfIndexEntry(config, entry);
	auto indexListSuffix = getIndexListSuffix(config, entry, '_');

	return basePath + "_" + indexListSuffix;
}

static vector< Cache::IndexDownloadRecord > getDownloadInfoFromRelease(
		const Config& config, const IndexEntry& indexEntry, const string& suffix)
{
	auto baseUri = getUriOfIndexEntry(indexEntry);
	// TODO: make cachefiles::getAlias* functions
	auto alias = indexEntry.uri + ' ' + indexEntry.distribution;

	vector< Cache::IndexDownloadRecord > result;

	try
	{
		string openError;
		auto releaseFilePath = getPathOfReleaseList(config, indexEntry);
		File releaseFile(releaseFilePath, "r", openError);
		if (!openError.empty())
		{
			fatal2(__("unable to open the file '%s': %s"), releaseFilePath, openError);
		}

		HashSums::Type currentHashSumType = HashSums::Count;
		// now we need to find if this variant is present in the release file
		string line;
		smatch m;
		while (!releaseFile.getLine(line).eof())
		{
			if (line.compare(0, 3, "MD5") == 0)
			{
				currentHashSumType = HashSums::MD5;
			}
			else if (line.compare(0, 4, "SHA1") == 0)
			{
				currentHashSumType = HashSums::SHA1;
			}
			else if (line.compare(0, 6, "SHA256") == 0)
			{
				currentHashSumType = HashSums::SHA256;
			}
			else if (line.find(suffix) != string::npos)
			{
				if (currentHashSumType == HashSums::Count)
				{
					fatal2(__("no hash sum declarations before the line '%s'"), line);
				}
				static sregex hashSumsLineRegex = sregex::compile("\\s([[:xdigit:]]+)\\s+(\\d+)\\s+(.*)");
				if (!regex_match(line, m, hashSumsLineRegex))
				{
					fatal2(__("malformed line '%s'"), line);
				}

				string name = m[3];
				if (name.compare(0, suffix.size(), suffix) != 0)
				{
					continue; // doesn't start with suffix
				}

				// filling result structure
				string uri = baseUri + '/' + name;
				bool foundRecord = false;
				FORIT(recordIt, result)
				{
					if (recordIt->uri == uri)
					{
						recordIt->hashSums[currentHashSumType] = m[1];
						foundRecord = true;
						break;
					}
				}
				if (!foundRecord)
				{
					Cache::IndexDownloadRecord& record =
							(result.push_back(Cache::IndexDownloadRecord()), *(result.rbegin()));
					record.uri = uri;
					record.size = string2uint32(m[2]);
					record.hashSums[currentHashSumType] = m[1];
				}
			}
		}
		// checks
		FORIT(recordIt, result)
		{
			if (recordIt->hashSums.empty())
			{
				fatal2(__("no hash sums defined for the index uri '%s'"), recordIt->uri);
			}
		}
	}
	catch (Exception&)
	{
		fatal2("unable to parse the release '%s'", alias);
	}

	return result;
}

vector< Cache::IndexDownloadRecord > getDownloadInfoOfIndexList(
		const Config& config, const IndexEntry& indexEntry)
{
	return getDownloadInfoFromRelease(config, indexEntry,
			getIndexListSuffix(config, indexEntry, '/'));
}

static vector< vector< string > > getChunksOfLocalizedDescriptions(
		const Config& config, const IndexEntry& entry)
{
	vector< vector< string > > result;

	if (entry.category != IndexEntry::Binary)
	{
		return result;
	}

	vector< string > chunksBase;
	if (!entry.component.empty())
	{
		chunksBase.push_back(entry.component);
	}
	chunksBase.push_back("i18n");

	set< string > alreadyAddedTranslations;
	auto addTranslation = [&chunksBase, &alreadyAddedTranslations, &result](const string& locale)
	{
		if (alreadyAddedTranslations.insert(locale).second)
		{
			auto chunks = chunksBase;
			chunks.push_back(string("Translation-") + locale);
			result.push_back(chunks);
		}
	};

	auto translationVariable = config.getString("cupt::languages::indexes");
	auto translations = split(',', translationVariable);
	FORIT(translationIt, translations)
	{
		auto locale = (*translationIt == "environment") ?
				setlocale(LC_MESSAGES, NULL) : *translationIt;
		if (locale == "none")
		{
			continue;
		}

		// cutting out an encoding
		auto dotPosition = locale.rfind('.');
		if (dotPosition != string::npos)
		{
			locale.erase(dotPosition);
		}
		addTranslation(locale);

		// cutting out an country specificator
		auto underlinePosition = locale.rfind('_');
		if (underlinePosition != string::npos)
		{
			locale.erase(underlinePosition);
		}
		addTranslation(locale);
	}

	return result;
}

vector< string > getPathsOfLocalizedDescriptions(const Config& config, const IndexEntry& entry)
{
	auto chunkArrays = getChunksOfLocalizedDescriptions(config, entry);
	auto basePath = getPathOfIndexEntry(config, entry);

	vector< string > result;
	FORIT(chunkArrayIt, chunkArrays)
	{
		result.push_back(basePath + "_" + join("_", *chunkArrayIt));
	}

	return result;
}

vector< Cache::LocalizationDownloadRecord > getDownloadInfoOfLocalizedDescriptions(
		const Config& config, const IndexEntry& entry)
{
	auto chunkArrays = getChunksOfLocalizedDescriptions(config, entry);
	auto basePath = getPathOfIndexEntry(config, entry);
	auto baseUri = getUriOfIndexEntry(entry);

	vector< Cache::LocalizationDownloadRecord > result;

	FORIT(chunkArrayIt, chunkArrays)
	{
		Cache::LocalizationDownloadRecord record;
		record.localPath = basePath + "_" + join("_", *chunkArrayIt);
		// yes, somewhy translations are always bzip2'ed
		record.uri = baseUri + "/" + join("/", *chunkArrayIt) + ".bz2";
		result.push_back(std::move(record));
	}

	return result;
}

vector< FileDownloadRecord > getDownloadInfoOfLocalizationIndex(const Config& config,
		const IndexEntry& entry)
{
	return getDownloadInfoFromRelease(config, entry, entry.component + "/i18n/Index");
}

vector< LocalizationDownloadRecord2 > getDownloadInfoOfLocalizedDescriptions2(
		const Config& config, const IndexEntry& entry)
{
	auto chunkArrays = getChunksOfLocalizedDescriptions(config, entry);
	auto basePath = getPathOfIndexEntry(config, entry);

	vector< LocalizationDownloadRecord2 > result;

	FORIT(chunkArrayIt, chunkArrays)
	{
		LocalizationDownloadRecord2 record;
		record.localPath = basePath + "_" + join("_", *chunkArrayIt);
		record.filePart = *(chunkArrayIt->rbegin()); // i.e. 'Translation-xyz' part
		result.push_back(std::move(record));
	}

	return result;
}

vector< LocalizationDownloadRecord3 > getDownloadInfoOfLocalizedDescriptions3(
		const Config& config, const IndexEntry& entry)
{
	auto chunkArrays = getChunksOfLocalizedDescriptions(config, entry);
	auto basePath = getPathOfIndexEntry(config, entry);

	vector< LocalizationDownloadRecord3 > result;

	for (const auto& chunkArray: chunkArrays)
	{
		LocalizationDownloadRecord3 record;
		record.fileDownloadRecords = getDownloadInfoFromRelease(config, entry,
				entry.component + "/i18n/" + chunkArray.back());
		if (record.fileDownloadRecords.empty())
		{
			continue;
		}
		record.localPath = basePath + "_" + join("_", chunkArray);

		record.language = chunkArray.back();
		auto slashPosition = record.language.find('-');
		if (slashPosition != string::npos)
		{
			record.language.erase(0, slashPosition + 1);
		}

		result.push_back(std::move(record));
	}

	return result;
}

string getPathOfExtendedStates(const Config& config)
{
	return config.getPath("dir::state::extendedstates");
}

bool verifySignature(const Config& config, const string& path, const string& alias)
{
	auto debugging = config.getBool("debug::gpgv");
	if (debugging)
	{
		debug2("verifying file '%s'", path);
	}

	auto keyringPath = config.getString("gpgv::trustedkeyring");
	if (debugging)
	{
		debug2("keyring file is '%s'", keyringPath);
	}

	auto signaturePath = path + ".gpg";
	if (debugging)
	{
		debug2("signature file is '%s'", signaturePath);
	}

	if (!fs::fileExists(signaturePath))
	{
		if (debugging)
		{
			debug2("signature file '%s' doesn't exist", signaturePath);
		}
		return 0;
	}

	// file checks
	{
		string openError;
		File file(signaturePath, "r", openError);
		if (!openError.empty())
		{
			if (debugging)
			{
				debug2("unable to read signature file '%s': %s", signaturePath, openError);
			}
			return false;
		}
	}
	{
		string openError;
		File file(keyringPath, "r", openError);
		if (!openError.empty())
		{
			if (debugging)
			{
				debug2("unable to read keyring file '%s': %s", keyringPath, openError);
			}
			return false;
		}
	}

	bool verifyResult = false;
	try
	{
		string gpgCommand = string("gpgv --status-fd 1 --keyring ") + keyringPath +
				' ' + signaturePath + ' ' + path + " 2>/dev/null || true";
		string openError;
		File gpgPipe(gpgCommand, "pr", openError);
		if (!openError.empty())
		{
			fatal2(__("unable to open the pipe '%s': %s"), gpgCommand, openError);
		}

		smatch m;
		auto gpgGetLine = [&gpgPipe, &m, &debugging]() -> string
		{
			static const sregex sigIdRegex = sregex::compile("\\[GNUPG:\\] SIG_ID");
			static const sregex generalRegex = sregex::compile("\\[GNUPG:\\] ");
			string result;
			do
			{
				gpgPipe.getLine(result);
				if (debugging && !gpgPipe.eof())
				{
					debug2("fetched '%s' from gpg pipe", result);
				}
			} while (!gpgPipe.eof() && (
						regex_search(result, m, sigIdRegex, regex_constants::match_continuous) ||
						!regex_search(result, m, generalRegex, regex_constants::match_continuous)));

			if (gpgPipe.eof())
			{
				return "";
			}
			else
			{
				return regex_replace(result, generalRegex, "");
			}
		};


		auto status = gpgGetLine();
		if (status.empty())
		{
			// no info from gpg at all
			fatal2(__("gpg: '%s': no information"), alias);
		}

		// first line ought to be validness indicator
		static const sregex messageRegex = sregex::compile("(\\w+) (.*)");
		if (!regex_match(status, m, messageRegex))
		{
			fatal2(__("gpg: '%s': invalid status string '%s'"), alias, status);
		}

		string messageType = m[1];
		string message = m[2];

		if (messageType == "GOODSIG")
		{
			string furtherInfo = gpgGetLine();
			if (!regex_match(furtherInfo, m, messageRegex))
			{
				fatal2(__("gpg: '%s': invalid detailed information string '%s'"), alias, furtherInfo);
			}

			string furtherInfoType = m[1];
			string furtherInfoMessage = m[2];
			if (furtherInfoType == "VALIDSIG")
			{
				// no comments :)
				verifyResult = 1;
			}
			else if (furtherInfoType == "EXPSIG")
			{
				warn2(__("gpg: '%s': expired signature: %s"), alias, furtherInfoMessage);
			}
			else if (furtherInfoType == "EXPKEYSIG")
			{
				warn2(__("gpg: '%s': expired key: %s"), alias, furtherInfoMessage);
			}
			else if (furtherInfoType == "REVKEYSIG")
			{
				warn2(__("gpg: '%s': revoked key: %s"), alias, furtherInfoMessage);
			}
			else
			{
				warn2(__("gpg: '%s': unknown error: %s %s"), alias, furtherInfoType, furtherInfoMessage);
			}
		}
		else if (messageType == "BADSIG")
		{
			warn2(__("gpg: '%s': bad signature: %s"), alias, message);
		}
		else if (messageType == "ERRSIG")
		{
			// gpg was not able to verify signature

			// maybe, public key was not found?
			bool publicKeyWasNotFound = false;
			auto detail = gpgGetLine();
			if (!detail.empty())
			{
				if (!regex_match(detail, m, messageRegex))
				{
					fatal2(__("gpg: '%s': invalid detailed information string '%s'"), alias, detail);
				}
				string detailType = m[1];
				string detailMessage = m[2];
				if (detailType == "NO_PUBKEY")
				{
					publicKeyWasNotFound = true;

					// the message looks like
					//
					// NO_PUBKEY D4F5CE00FA0E9B9D
					//
					warn2(__("gpg: '%s': public key '%s' is not found"), alias, detailMessage);
				}
			}

			if (!publicKeyWasNotFound)
			{
				warn2(__("gpg: '%s': could not verify a signature: %s"), alias, message);
			}
		}
		else if (messageType == "NODATA")
		{
			// no signature
			warn2(__("gpg: '%s': empty signature"), alias);
		}
		else if (messageType == "KEYEXPIRED")
		{
			warn2(__("gpg: '%s': expired key: %s"), alias, message);
		}
		else
		{
			warn2(__("gpg: '%s': unknown message: %s %s"), alias, messageType, message);
		}
	}
	catch (Exception&)
	{
		warn2(__("unable to verify a signature for the '%s'"), alias);
	}

	if (debugging)
	{
		debug2("the verify result is %u", (unsigned int)verifyResult);
	}
	return verifyResult;
}

shared_ptr< cache::ReleaseInfo > getReleaseInfo(const Config& config,
		const string& path, const string& alias)
{
	shared_ptr< cache::ReleaseInfo > result(new cache::ReleaseInfo);
	result->notAutomatic = false; // default
	result->butAutomaticUpgrades = false; // default

	static sregex fieldRegex = sregex::compile("^((?:\\w|-)+?): (.*)"); // $ implied in regex
	smatch matches;
	try
	{
		string openError;
		File file(path, "r", openError);
		if (!openError.empty())
		{
			fatal2(__("unable to open the file '%s': %s"), path, openError);
		}

		string line;
		while (! file.getLine(line).eof())
		{
			if (!regex_match(line, matches, fieldRegex))
			{
				break;
			}
			string fieldName = matches[1];
			string fieldValue = matches[2];

			if (fieldName == "Origin")
			{
				result->vendor = fieldValue;
			}
			else if (fieldName == "Label")
			{
				result->label = fieldValue;
			}
			else if (fieldName == "Suite")
			{
				result->archive = fieldValue;
			}
			else if (fieldName == "Codename")
			{
				result->codename = fieldValue;
			}
			else if (fieldName == "Date")
			{
				result->date = fieldValue;
			}
			else if (fieldName == "Valid-Until")
			{
				result->validUntilDate = fieldValue;
			}
			else if (fieldName == "NotAutomatic")
			{
				result->notAutomatic = true;
			}
			else if (fieldName == "ButAutomaticUpgrades")
			{
				result->butAutomaticUpgrades = true;
			}
			else if (fieldName == "Architectures")
			{
				result->architectures = split(' ', fieldValue);
			}
			else if (fieldName == "Version")
			{
				result->version = fieldValue;
			}
			else if (fieldName == "Description")
			{
				result->description = fieldValue;
				if (result->version.empty())
				{
					smatch descriptionMatch;
					if (regex_search(fieldValue, descriptionMatch, sregex::compile("[0-9][0-9a-z._-]*")))
					{
						result->version = descriptionMatch[0];
					}
				}
			}
		}
	}
	catch (Exception&)
	{
		fatal2(__("unable to parse the release '%s'"), alias);
	}

	{ // checking Valid-Until
		if (!result->validUntilDate.empty())
		{
			struct tm validUntilTm;
			memset(&validUntilTm, 0, sizeof(validUntilTm));
			struct tm currentTm;

			auto oldTimeSpec = setlocale(LC_TIME, "C");
			auto parseResult = strptime(result->validUntilDate.c_str(), "%a, %d %b %Y %T UTC", &validUntilTm);
			setlocale(LC_TIME, oldTimeSpec);
			if (parseResult) // success
			{
				time_t localTime = time(NULL);
				gmtime_r(&localTime, &currentTm);
				// sanely, we should use timegm() here, but it's not portable,
				// so we use mktime() which is enough for comparing two UTC tm's
				if (mktime(&currentTm) > mktime(&validUntilTm))
				{
					bool warnOnly = config.getBool("cupt::cache::release-file-expiration::ignore");
					(warnOnly ? warn2< string, string > : fatal2< string, string >)
							(__("the release '%s' has expired (expiry time '%s')"), alias, result->validUntilDate);
				}
			}
			else
			{
				warn2(__("unable to parse the expiry time '%s' in the release '%s'"),
						result->validUntilDate, alias);
			}
		}
	}

	return result;
}

}
}
}

