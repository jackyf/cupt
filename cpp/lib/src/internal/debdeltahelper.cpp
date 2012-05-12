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

#include <cupt/cache.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/binaryversion.hpp>
#include <cupt/cache/releaseinfo.hpp>
#include <cupt/file.hpp>

#include <internal/debdeltahelper.hpp>
#include <internal/filesystem.hpp>

namespace cupt {
namespace internal {

DebdeltaHelper::DebdeltaHelper()
{
	if (fs::fileExists("/usr/bin/debpatch"))
	{
		// fill debdelta sources only if patches is available
		static const string sourcesPath = "/etc/debdelta/sources.conf";
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

	auto mangleVersionString = [](const string& input)
	{
		// I hate http uris, hadn't I told this before, hm...
		const string doubleEscapedColon = "%253a";

		string result;
		// replacing
		FORIT(charIt, input)
		{
			char c = *charIt;
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
	};

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

			bool found = false;
			FORIT(sourceIt, version->sources)
			{
				const shared_ptr< const ReleaseInfo >& releaseInfo = sourceIt->release;
				string releaseValue;
				if (key == "Origin")
				{
					releaseValue = releaseInfo->vendor;
				}
				else if (key == "Label")
				{
					releaseValue = releaseInfo->label;
				}
				else if (key == "Archive")
				{
					releaseValue = releaseInfo->archive;
				}
				else
				{
					continue;
				}

				if (releaseValue == value)
				{
					found = true;
					break;
				}
			}

			if (!found)
			{
				goto next_source;
			}
		}

		{
			// suitable
			string baseUri = "debdelta:" + deltaUriIt->second;

			// not very reliable :(
			string appendage = version->sources[0].directory + '/';
			appendage += join("_", vector< string >{ packageName,
					mangleVersionString(installedVersion->versionString),
					mangleVersionString(version->versionString),
					version->architecture });
			appendage += ".debdelta";

			DownloadRecord record;
			record.baseUri = baseUri;
			record.uri = baseUri + '/' + appendage;
			result.push_back(std::move(record));
		}

		next_source:
		;
	}

	return result;
}

void DebdeltaHelper::__parse_sources(const string& path)
{
	string openError;
	File file(path, "r", openError);
	if (!openError.empty())
	{
		fatal2(__("unable to open the file '%s': %s"), path, openError);
	}


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

