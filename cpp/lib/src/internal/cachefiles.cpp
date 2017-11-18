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

string getPathOfInReleaseList(const Config& config, const IndexEntry& entry)
{
	return getPathOfIndexEntry(config, entry) + "_InRelease";
}

namespace {

string selectNewerFile(const string& leftPath, const string& rightPath)
{
	bool leftExists = fs::fileExists(leftPath);
	bool rightExists = fs::fileExists(rightPath);
	if (!leftExists && !rightExists) return string();
	if (!rightExists) return leftPath;
	if (!leftExists) return rightPath;
	return fs::fileModificationTime(leftPath) >= fs::fileModificationTime(rightPath) ?
			leftPath : rightPath;
}

}

string getPathOfMasterReleaseLikeList(const Config& config, const IndexEntry& entry)
{
	return selectNewerFile(
			getPathOfInReleaseList(config, entry),
			getPathOfReleaseList(config, entry));
}

string getDownloadUriOfReleaseList(const IndexEntry& entry)
{
	return getUriOfIndexEntry(entry) + "/Release";
}

string getDownloadUriOfInReleaseList(const IndexEntry& entry)
{
	return getUriOfIndexEntry(entry) + "/InRelease";
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

static vector< FileDownloadRecord > getDownloadInfoFromRelease(
		const Config& config, const IndexEntry& indexEntry, const string& suffix)
{
	auto baseUri = getUriOfIndexEntry(indexEntry);
	// TODO: make cachefiles::getAlias* functions
	auto alias = indexEntry.uri + ' ' + indexEntry.distribution;

	vector< FileDownloadRecord > result;

	try
	{
		auto releaseFilePath = getPathOfMasterReleaseLikeList(config, indexEntry);
		RequiredFile releaseFile(releaseFilePath, "r");

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
			else if (line.empty() || !isspace(line[0])) // end of hash sum block
			{
				currentHashSumType = HashSums::Count;
			}
			else if (currentHashSumType != HashSums::Count && line.find(suffix) != string::npos)
			{
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
					FileDownloadRecord& record =
							(result.push_back(FileDownloadRecord()), *(result.rbegin()));
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
				fatal2(__("no hash sums defined for the index URI '%s'"), recordIt->uri);
			}
		}
	}
	catch (Exception&)
	{
		fatal2("unable to parse the release '%s'", alias);
	}

	return result;
}

vector< FileDownloadRecord > getDownloadInfoOfIndexList(
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

static string extractLocalizationLanguage(const string& lastChunk)
{
	string result = lastChunk;
	auto dashPosition = result.find('-');
	if (dashPosition != string::npos)
	{
		result.erase(0, dashPosition + 1);
	}
	return result;
}

vector< pair< string, string > > getPathsOfLocalizedDescriptions(
		const Config& config, const IndexEntry& entry)
{
	auto chunkArrays = getChunksOfLocalizedDescriptions(config, entry);
	auto basePath = getPathOfIndexEntry(config, entry);

	vector< pair< string, string > > result;
	for (const auto& chunkArray: chunkArrays)
	{
		auto path = basePath + "_" + join("_", chunkArray);
		result.push_back({ extractLocalizationLanguage(chunkArray.back()), std::move(path) });
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
		record.language = extractLocalizationLanguage(chunkArray.back());

		result.push_back(std::move(record));
	}

	return result;
}

string getPathOfExtendedStates(const Config& config)
{
	return config.getPath("dir::state::extendedstates");
}

namespace {

bool openingForReadingSucceeds(const string& path)
{
	string openError;
	File file(path, "r", openError);
	return openError.empty();
	return true;
}

string composeGpgvKeyringOptions(const Config& config)
{
	auto debugging = config.getBool("debug::gpgv");

	string result;
	auto considerKeyring = [&](const string& keyring)
	{
		if (debugging) debug2("keyring file is '%s'", keyring);
		result += format2(" --keyring %s", keyring);
	};

	considerKeyring(config.getPath("dir::etc::trusted"));
	for (const auto& keyring: config.getConfigurationPartPaths("dir::etc::trustedparts"))
	{
		considerKeyring(keyring);
	}

	return result;
}

string composeGpgvCommand(const Config& config, const string& path)
{
	auto debugging = config.getBool("debug::gpgv");

	auto signaturePath = path + ".gpg";
	if (debugging) debug2("signature file is '%s'", signaturePath);

	if (!fs::fileExists(signaturePath))
	{
		if (debugging) debug2("signature file '%s' doesn't exist, omitting it and assuming self-signed file", signaturePath);
		signaturePath.clear();
	}
	else if (!openingForReadingSucceeds(signaturePath))
	{
		if (debugging) debug2("signature file '%s' is not accessible", signaturePath);
		signaturePath.clear();
	}

	return format2("gpgv --status-fd 1 %s %s %s 2>/dev/null || true",
			composeGpgvKeyringOptions(config), signaturePath, path);
}

bool isNotGoodSignature(const string& alias, const string& messageType, const string& message)
{
	if (messageType == "EXPSIG")
	{
		warn2(__("gpg: '%s': expired signature: %s"), alias, message);
	}
	else if (messageType == "EXPKEYSIG")
	{
		warn2(__("gpg: '%s': expired key: %s"), alias, message);
	}
	else if (messageType == "REVKEYSIG")
	{
		warn2(__("gpg: '%s': revoked key: %s"), alias, message);
	}
	else if (messageType == "BADSIG")
	{
		warn2(__("gpg: '%s': bad signature: %s"), alias, message);
	}
	else if (messageType == "ERRSIG")
	{
		// gpg was not able to verify signature
		auto parts = split(' ', message); // <keyid> <pkalgo> <hashalgo> <sig_class> <time> <rc>
		if (parts.size() != 6)
		{
			fatal2(__("gpg: '%s': invalid detailed information string '%s'"), alias, message);
		}
		const auto& rc = parts.back();

		if (rc == "9")
		{
			warn2(__("gpg: '%s': public key '%s' is not found"), alias, parts[0]);
		}
		else if (rc == "4")
		{
			warn2(__("gpg: '%s': unknown algorithm '%s/%s'"), alias, parts[1], parts[2]);
		}
		else
		{
			warn2(__("gpg: '%s': could not verify a signature: %s"), alias, message);
		}
	}
	else if (messageType == "NODATA")
	{
		warn2(__("gpg: '%s': empty signature"), alias);
	}
	else
	{
		return false;
	}
	return true;
}

bool runGpgCommand(const string& gpgCommand, const string& alias, bool debugging)
{
	if (debugging) debug2("gpgv command is '%s'", gpgCommand);
	try
	{
		string openError;
		File gpgPipe(gpgCommand, "pr", openError);
		if (!openError.empty())
		{
			fatal2(__("unable to open the pipe '%s': %s"), gpgCommand, openError);
		}

		smatch m;
		static const sregex gnupgPrefixRegex = sregex::compile("\\[GNUPG:\\] ");

		string status;
		while (!gpgPipe.getLine(status).eof())
		{
			if (debugging) debug2("fetched '%s' from gpg pipe", status);
			status = regex_replace(status, gnupgPrefixRegex, "");

			if (status.empty())
			{
				fatal2(__("gpg: '%s': no information"), alias);
			}

			static const sregex messageRegex = sregex::compile("(\\w+) (.*)");
			if (!regex_match(status, m, messageRegex))
			{
				continue; // not a status string / message
			}

			string messageType = m[1];
			string message = m[2];

			if (messageType == "GOODSIG")
			{
				return true;
			}
			else if (isNotGoodSignature(alias, messageType, message))
			{
				return false;
			}
		}
	}
	catch (Exception&)
	{
		warn2(__("unable to verify a signature for the '%s'"), alias);
	}
	return false;
}

}

bool verifySignature(const Config& config, const string& path, const string& alias)
{
	const auto debugging = config.getBool("debug::gpgv");
	if (debugging) debug2("verifying file '%s'", path);

	const auto gpgCommand = composeGpgvCommand(config, path);
	const bool verifyResult = runGpgCommand(gpgCommand, alias, debugging);

	if (debugging) debug2("the verify result is %u", (unsigned int)verifyResult);
	return verifyResult;
}

void verifyReleaseValidityDate(const string& date, const Config& config, const string& releaseAlias)
{
	if (date.empty()) {
		return;
	}

	struct tm validUntilTm;
	memset(&validUntilTm, 0, sizeof(validUntilTm));
	struct tm currentTm;

	auto oldTimeSpec = setlocale(LC_TIME, "C");
	auto parseResult = strptime(date.c_str(), "%a, %d %b %Y %T UTC", &validUntilTm);
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
					(__("the release '%s' has expired (expiry time '%s')"), releaseAlias, date);
		}
	}
	else
	{
		warn2(__("unable to parse the expiry time '%s' in the release '%s'"), date, releaseAlias);
	}
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
		RequiredFile file(path, "r");

		string line;
		while (! file.getLine(line).eof())
		{
			if (line.empty()) continue;
			if (line[0] == '-') continue; // "----- BEGIN PGP SIGNED MESSAGE-----"
			if (!regex_match(line, matches, fieldRegex)) break;

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

	return result;
}

}
}
}

