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

#include <common/regex.hpp>

#include <cupt/config.hpp>
#include <cupt/file.hpp>

#include <internal/cachefiles.hpp>
#include <internal/common.hpp>

namespace cupt {
namespace internal {
namespace cachefiles {

static string getPathOfIndexEntry(const Config& config, const IndexEntry& entry)
{
	// "http://ftp.ua.debian.org" -> "ftp.ua.debian.org"
	// "file:/home/jackyf" -> "/home/jackyf"
	static sregex schemeRegex = sregex::compile("^\\w+:(?://)?");
	string uriPrefix = regex_replace(entry.uri, schemeRegex, "");

	// "escaping" tilde, following APT practice :(
	static sregex tildeRegex = sregex::compile("~");
	uriPrefix = regex_replace(uriPrefix, tildeRegex, "%7e");

	// "ftp.ua.debian.org/debian" -> "ftp.ua.debian.org_debian"
	static sregex slashRegex = sregex::compile("/");
	uriPrefix = regex_replace(uriPrefix, slashRegex, "_");

	string directory = config.getPath("cupt::directory::state::lists");

	string distributionPart = regex_replace(entry.distribution, slashRegex, "_");

	string basePart = uriPrefix + "_";
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

				string diffNamePattern = indexListSuffix + ".diff";
				if (name.compare(0, diffNamePattern.size(), diffNamePattern) == 0)
				{
					continue; // skipping diffs for now...
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

}
}
}

