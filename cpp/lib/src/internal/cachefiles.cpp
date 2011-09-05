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

vector< Cache::IndexDownloadRecord > getDownloadInfoOfIndexList(
		const Config& config, const IndexEntry& indexEntry)
{
	auto baseUri = getUriOfIndexEntry(indexEntry);
	auto indexListSuffix = getIndexListSuffix(config, indexEntry, '/');

	vector< Cache::IndexDownloadRecord > result;
	{ // reading
		string openError;
		auto releaseFilePath = getPathOfReleaseList(config, indexEntry);
		File releaseFile(releaseFilePath, "r", openError);
		if (!openError.empty())
		{
			fatal("unable to open release file '%s': %s",
					releaseFilePath.c_str(), openError.c_str());
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
			else if (line.find(indexListSuffix) != string::npos)
			{
				if (currentHashSumType == HashSums::Count)
				{
					fatal("release line '%s' without previous hash sum declaration at release file '%s'",
								line.c_str(), releaseFilePath.c_str());
				}
				static sregex hashSumsLineRegex = sregex::compile("\\s([[:xdigit:]]+)\\s+(\\d+)\\s+(.*)");
				if (!regex_match(line, m, hashSumsLineRegex))
				{
					fatal("malformed release line '%s' at file '%s'",
								line.c_str(), releaseFilePath.c_str());
				}

				string name = m[3];
				if (name.compare(0, indexListSuffix.size(), indexListSuffix) != 0)
				{
					continue; // doesn't start with indexListSuffix
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
	}

	// checks
	FORIT(recordIt, result)
	{
		if (recordIt->hashSums.empty())
		{
			fatal("no hash sums defined for index list URI '%s'", recordIt->uri.c_str());
		}
	}

	return result;
}

static vector< vector< string > > getChunksOfLocalizedDescriptions(
		const Config& config, const IndexEntry& entry)
{
	vector< vector< string > > result;

	if (entry.category != IndexEntry::Binary)
	{
		return result;
	}

	auto translationVariable = config.getString("apt::acquire::translation");
	auto locale = translationVariable == "environment" ?
			setlocale(LC_MESSAGES, NULL) : translationVariable;
	if (locale == "none")
	{
		return result;
	}

	vector< string > chunks;
	if (!entry.component.empty())
	{
		chunks.push_back(entry.component);
	}
	chunks.push_back("i18n");

	result.push_back(chunks);
	// cutting out an encoding
	auto dotPosition = locale.rfind('.');
	if (dotPosition != string::npos)
	{
		locale.erase(dotPosition);
	}
	result[0].push_back(string("Translation-") + locale);

	result.push_back(chunks);
	// cutting out an country specificator
	auto underlinePosition = locale.rfind('_');
	if (underlinePosition != string::npos)
	{
		locale.erase(underlinePosition);
	}
	result[1].push_back(string("Translation-") + locale);

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

string getPathOfExtendedStates(const Config& config)
{
	return config.getPath("dir::state::extendedstates");
}

bool verifySignature(const Config& config, const string& path)
{
	auto debugging = config.getBool("debug::gpgv");
	if (debugging)
	{
		debug("verifying file '%s'", path.c_str());
	}

	auto keyringPath = config.getString("gpgv::trustedkeyring");
	if (debugging)
	{
		debug("keyring file is '%s'", keyringPath.c_str());
	}

	auto signaturePath = path + ".gpg";
	if (debugging)
	{
		debug("signature file is '%s'", signaturePath.c_str());
	}

	if (!fs::fileExists(signaturePath))
	{
		if (debugging)
		{
			debug("signature file '%s' doesn't exist", signaturePath.c_str());
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
				debug("unable to read signature file '%s': %s",
						signaturePath.c_str(), openError.c_str());
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
				debug("unable to read keyring file '%s': %s",
						keyringPath.c_str(), openError.c_str());
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
			fatal("unable to open gpg pipe: %s", openError.c_str());
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
					debug("fetched '%s' from gpg pipe", result.c_str());
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
			fatal("gpg: '%s': no info received", path.c_str());
		}

		// first line ought to be validness indicator
		static const sregex messageRegex = sregex::compile("(\\w+) (.*)");
		if (!regex_match(status, m, messageRegex))
		{
			fatal("gpg: '%s': invalid status string '%s'", path.c_str(), status.c_str());
		}

		string messageType = m[1];
		string message = m[2];

		if (messageType == "GOODSIG")
		{
			string furtherInfo = gpgGetLine();
			if (furtherInfo.empty())
			{
				fatal("gpg: '%s': error: unfinished status", path.c_str());
			}

			if (!regex_match(furtherInfo, m, messageRegex))
			{
				fatal("gpg: '%s': invalid further info string '%s'", path.c_str(), furtherInfo.c_str());
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
				warn("gpg: '%s': expired signature: %s", path.c_str(), furtherInfoMessage.c_str());
			}
			else if (furtherInfoType == "EXPKEYSIG")
			{
				warn("gpg: '%s': expired key: %s", path.c_str(), furtherInfoMessage.c_str());
			}
			else if (furtherInfoType == "REVKEYSIG")
			{
				warn("gpg: '%s': revoked key: %s", path.c_str(), furtherInfoMessage.c_str());
			}
			else
			{
				warn("gpg: '%s': unknown error: %s %s",
						path.c_str(), furtherInfoType.c_str(), furtherInfoMessage.c_str());
			}
		}
		else if (messageType == "BADSIG")
		{
			warn("gpg: '%s': bad signature: %s", path.c_str(), message.c_str());
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
					fatal("gpg: '%s': invalid detailed info string '%s'", path.c_str(), detail.c_str());
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
					warn("gpg: '%s': public key '%s' not found", path.c_str(), detailMessage.c_str());
				}
			}

			if (!publicKeyWasNotFound)
			{
				warn("gpg: '%s': could not verify signature: %s", path.c_str(), message.c_str());
			}
		}
		else if (messageType == "NODATA")
		{
			// no signature
			warn("gpg: '%s': empty signature", path.c_str());
		}
		else if (messageType == "KEYEXPIRED")
		{
			warn("gpg: '%s': expired key: %s", path.c_str(), message.c_str());
		}
		else
		{
			warn("gpg: '%s': unknown message received: %s %s",
					path.c_str(), messageType.c_str(), message.c_str());
		}
	}
	catch (Exception&)
	{
		warn("error while verifying signature for file '%s'", path.c_str());
	}

	if (debugging)
	{
		debug("the verify result is %u", (unsigned int)verifyResult);
	}
	return verifyResult;
}

shared_ptr< cache::ReleaseInfo > getReleaseInfo(const Config& config, const string& path)
{
	shared_ptr< cache::ReleaseInfo > result(new cache::ReleaseInfo);
	result->notAutomatic = false; // default

	string openError;
	File file(path, "r", openError);
	if (!openError.empty())
	{
		fatal("unable to open release file '%s': EEE", path.c_str());
	}

	size_t lineNumber = 1;
	static sregex fieldRegex = sregex::compile("^((?:\\w|-)+?): (.*)"); // $ implied in regex
	smatch matches;
	try
	{
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
			else if (fieldName == "Architectures")
			{
				result->architectures = split(' ', fieldValue);
			}
			else if (fieldName == "Description")
			{
				result->description = fieldValue;
				smatch descriptionMatch;
				if (regex_search(fieldValue, descriptionMatch, sregex::compile("[0-9][0-9a-z._-]*")))
				{
					result->version = descriptionMatch[0];
				}
			}
		}
		++lineNumber;
	}
	catch (Exception&)
	{
		fatal("error parsing release file '%s', line %u", path.c_str(), lineNumber);
	}

	if (result->vendor.empty())
	{
		warn("no vendor specified in release file '%s'", path.c_str());
	}
	if (result->archive.empty())
	{
		warn("no archive specified in release file '%s'", path.c_str());
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
					(warnOnly ? warn : fatal)("release file '%s' has expired (expiry time '%s')",
							path.c_str(), result->validUntilDate.c_str());
				}
			}
			else
			{
				warn("unable to parse expiry time '%s' in release file '%s'",
						result->validUntilDate.c_str(), path.c_str());
			}
		}
	}

	return result;
}

}
}
}

