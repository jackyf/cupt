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
#include <common/regex.hpp>

#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/binaryversion.hpp>
#include <cupt/cache/releaseinfo.hpp>
#include <cupt/file.hpp>
#include <cupt/versionstring.hpp>

#include <internal/debdeltahelper.hpp>
#include <internal/filesystem.hpp>

namespace cupt {
namespace internal {

DebdeltaHelper::DebdeltaHelper(const Config& config)
{
	auto prefixDir = config.getPath("dir");

	if (fs::fileExists(prefixDir + "/usr/bin/debpatch"))
	{
		// fill debdelta sources only if patches is available
		const string sourcesPath = prefixDir + "/etc/debdelta/sources.conf";
		if (fs::fileExists(sourcesPath))
		{
			try
			{
				__parse_sources(sourcesPath);
			}
			catch (Exception& e)
			{
				warn2(__("failed to parse the debdelta configuration file '%s'"), sourcesPath);
			}
		}
	}
}

static string httpMangleVersionString(StringRange input)
{
	// I hate http uris, hadn't I told this before, hm...
	const string doubleEscapedColon = "%253a";

	string result;
	// replacing
	for (const char c: input)
	{
		if (c != ':')
		{
			result += c;
		}
		else
		{
			result += doubleEscapedColon;
		}
	}

	return result;
}

static DebdeltaHelper::DownloadRecord generateDownloadRecord(
		const string& deltaUri, const BinaryVersion* version, const Version* installedVersion)
{
	string baseUri = "debdelta:" + deltaUri;

	// not very reliable :(
	string appendage = version->sources[0].directory + '/';
	appendage += join("_", vector< string >{ version->packageName,
			httpMangleVersionString(getOriginalVersionString(installedVersion->versionString)),
			httpMangleVersionString(version->versionString),
			version->architecture });
	appendage += ".debdelta";

	DebdeltaHelper::DownloadRecord record;
	record.baseUri = baseUri;
	record.uri = baseUri + '/' + appendage;

	return record;
}

static bool isReleasePropertyPresent(const ReleaseInfo& releaseInfo,
		const string& key, const string& value)
{
	const string* releaseValue;
	if (key == "Origin")
	{
		releaseValue = &releaseInfo.vendor;
	}
	else if (key == "Label")
	{
		releaseValue = &releaseInfo.label;
	}
	else if (key == "Archive")
	{
		releaseValue = &releaseInfo.archive;
	}
	else
	{
		return false;
	}

	return (*releaseValue == value);
}

static bool isReleasePropertyPresent(const Version* version,
		const string& key, const string& value)
{
	for (const auto& source: version->sources)
	{
		if (isReleasePropertyPresent(*source.release, key, value))
		{
			return true;
		}
	}
	return false;
}

vector< DebdeltaHelper::DownloadRecord > DebdeltaHelper::getDownloadInfo(
		const cache::BinaryVersion* version,
		const shared_ptr< const Cache >& cache)
{
	vector< DownloadRecord > result;

	const string& packageName = version->packageName;
	auto package = cache->getBinaryPackage(packageName);
	if (!package)
	{
		warn2(__("debdeltahelper: received a version without a corresponding binary package in the cache: "
				"package '%s', version '%s'"), packageName, version->versionString);
		return result;
	}
	auto installedVersion = package->getInstalledVersion();
	if (!installedVersion)
	{
		return result; // nothing to try
	}

	FORIT(sourceIt, __sources)
	{
		const map< string, string >& sourceMap = sourceIt->second;
		auto deltaUriIt = sourceMap.find("delta_uri");
		if (deltaUriIt == sourceMap.end())
		{
			continue;
		}

		FORIT(keyValueIt, sourceMap)
		{
			const string& key = keyValueIt->first;
			if (key == "delta_uri")
			{
				continue;
			}
			const string& value = keyValueIt->second;

			if (!isReleasePropertyPresent(version, key, value))
			{
				goto next_source;
			}
		}

		// suitable
		result.push_back(generateDownloadRecord(deltaUriIt->second, version, installedVersion));

		next_source:
		;
	}

	return result;
}

void DebdeltaHelper::__parse_sources(const string& path)
{
	RequiredFile file(path, "r");

	/* we are parsing entries like that:
	 [main debian sources]
	 Origin=Debian
	 Label=Debian
	 delta_uri=http://www.bononia.it/debian-deltas
	*/

	string currentSection;
	string line;
	smatch m;
	while (!file.getLine(line).eof())
	{
		// skip empty lines and lines with comments
		static const sregex emptyAndCommentRegex = sregex::compile("^\\s*(#|$)", regex_constants::optimize);
		if (regex_search(line, m, emptyAndCommentRegex))
		{
			continue;
		}

		static const sregex sectionTitleRegex = sregex::compile("\\[(.*)\\]", regex_constants::optimize);
		if (regex_match(line, m, sectionTitleRegex))
		{
			// new section
			currentSection = m[1];
		}
		else
		{
			static const sregex keyValueRegex = sregex::compile("(.*?)=(.*)", regex_constants::optimize);
			if (!regex_match(line, m, keyValueRegex))
			{
				fatal2(__("unable to parse the key-value pair '%s' in the file '%s'"), line, path);
			}
			string key = m[1];
			string value = m[2];

			__sources[currentSection][key] = value;
		}
	}
}

}
}

