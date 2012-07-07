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
#include <boost/lexical_cast.hpp>

#include <common/regex.hpp>

#include <cupt/cache/version.hpp>
#include <cupt/file.hpp>
#include <cupt/config.hpp>
#include <cupt/cache/releaseinfo.hpp>
#include <cupt/cache/binaryversion.hpp>
#include <cupt/system/state.hpp>
#include <cupt/download/uri.hpp>

#include <internal/pininfo.hpp>
#include <internal/filesystem.hpp>
#include <internal/common.hpp>
#include <internal/regex.hpp>

namespace cupt {
namespace internal {

using cache::Version;
using cache::BinaryVersion;
using cache::ReleaseInfo;

PinInfo::PinInfo(const shared_ptr< const Config >& config,
		const shared_ptr< const system::State >& systemState)
	: config(config), systemState(systemState)
{
	init();
}

ssize_t PinInfo::getOriginalAptPin(const Version* version) const
{
	static const ssize_t defaultReleasePriority = 990;
	static const ssize_t notAutomaticReleasePriority = 1;
	static const ssize_t installedPriority = 100;
	static const ssize_t defaultPriority = 500;

	auto defaultRelease = config->getString("apt::default-release");

	// these are Cupt-specific
	ssize_t notAutomaticAddendum = config->getInteger("cupt::cache::pin::addendums::not-automatic");
	ssize_t butAutomaticUpgradesAddendum = config->getInteger("cupt::cache::pin::addendums::but-automatic-upgrades");

	ssize_t result = std::min((ssize_t)0, notAutomaticAddendum);

	size_t sourceCount = version->sources.size();
	for (size_t i = 0; i < sourceCount; ++i)
	{
		const Version::Source& entry = version->sources[i];
		auto currentPriority = defaultPriority;
		if (!defaultRelease.empty() &&
			(entry.release->archive == defaultRelease || entry.release->codename == defaultRelease))
		{
			currentPriority = defaultReleasePriority;
		}
		else if (entry.release->notAutomatic)
		{
			currentPriority = notAutomaticReleasePriority + notAutomaticAddendum;
			if (entry.release->butAutomaticUpgrades)
			{
				currentPriority += butAutomaticUpgradesAddendum;
			}
		}
		else if (entry.release->archive == "installed")
		{
			currentPriority = installedPriority;
		}

		if (result < currentPriority)
		{
			result = currentPriority;
		}
	}

	adjustUsingPinSettings(version, result);

	return result;
}

ssize_t PinInfo::getPin(const Version* version, const string& installedVersionString) const
{
	auto result = getOriginalAptPin(version);

	// adjust for downgrades and holds
	if (!installedVersionString.empty())
	{
		auto installedInfo = systemState->getInstalledInfo(version->packageName);
		if (!installedInfo)
		{
			fatal2i("missing installed info for package '%s'", version->packageName);
		}

		if (compareVersionStrings(installedVersionString, version->versionString) > 0)
		{
			result += config->getInteger("cupt::cache::pin::addendums::downgrade");
		}

		auto binaryVersion = dynamic_cast< const BinaryVersion* >(version);
		if (!binaryVersion)
		{
			fatal2i("version is not binary");
		}
		if (installedInfo->want == system::State::InstalledRecord::Want::Hold && binaryVersion->isInstalled())
		{
			result += config->getInteger("cupt::cache::pin::addendums::hold");
		}
	}

	if (version->isVerified())
	{
		result += 1;
	}

	return result;
}

string pinStringToRegexString(const string& input)
{
	if (input.size() >= 2 && input[0] == '/' && *input.rbegin() == '/')
	{
		return input.substr(1, input.size() - 2);
	}
	else
	{
		return globToRegexString(input);
	}
}

void PinInfo::loadData(const string& path)
{
	using boost::lexical_cast;

	// we are parsing triads like:

	// Package: perl perl-modules
	// Pin: o=debian,a=unstable
	// Pin-Priority: 800

	// Source: unetbootin
	// Pin: a=experimental
	// Pin-Priority: 1100

	string openError;
	File file(path, "r", openError);
	if (!openError.empty())
	{
		fatal2(__("unable to open the file '%s': %s"), path, openError);
	}

	smatch m;

	string line;
	size_t lineNumber = 0;
	auto getNextLine = [&file, &line, &lineNumber]() -> File&
	{
		file.getLine(line);
		++lineNumber;
		return file;
	};
	try
	{
		while (!getNextLine().eof())
		{
			// skip all empty lines and lines with comments
			static const sregex commentRegex = sregex::compile("\\s*(?:#.*)?");
			if (regex_match(line, m, commentRegex))
			{
				continue;
			}

			// skip special explanation lines, they are just comments
			static const sregex explanationRegex = sregex::compile("Explanation:");
			if (regex_search(line, m, explanationRegex, regex_constants::match_continuous))
			{
				continue;
			}

			// ok, real triad should be here
			PinEntry pinEntry;

			{ // processing first line
				PinEntry::Condition condition;
				static const sregex packageOrSourceRegex = sregex::compile("(Package|Source): (.*)");
				if (!regex_match(line, m, packageOrSourceRegex))
				{
					fatal2(__("invalid package/source line"));
				}

				condition.type = (string(m[1]) == "Package" ?
						PinEntry::Condition::PackageName : PinEntry::Condition::SourcePackageName);

				vector< string > parts = split(' ', m[2]);
				FORIT(it, parts)
				{
					*it = pinStringToRegexString(*it);
				}
				condition.value = stringToRegex(join("|", parts));
				pinEntry.conditions.push_back(std::move(condition));
			}

			{ // processing second line
				getNextLine();
				if (file.eof())
				{
					fatal2(__("no pin line"));
				}

				static const sregex pinRegex = sregex::compile("Pin: (\\w+?) (.*)");
				if (!regex_match(line, m, pinRegex))
				{
					fatal2(__("invalid pin line"));
				}

				string pinType = m[1];
				string pinExpression = m[2];
				if (pinType == "release")
				{
					static const sregex commaSeparatedRegex = sregex::compile("\\s*,\\s*");
					auto subExpressions = internal::split(commaSeparatedRegex, pinExpression);

					FORIT(subExpressionIt, subExpressions)
					{
						PinEntry::Condition condition;

						static const sregex subExpressionRegex = sregex::compile("(\\w)=(.*)");
						if (!regex_match(*subExpressionIt, m, subExpressionRegex))
						{
							fatal2(__("invalid condition '%s'"), *subExpressionIt);
						}

						char subExpressionType = string(m[1])[0]; // if regex matched, it is one-letter string
						switch (subExpressionType)
						{
							case 'a': condition.type = PinEntry::Condition::ReleaseArchive; break;
							case 'v': condition.type = PinEntry::Condition::ReleaseVersion; break;
							case 'c': condition.type = PinEntry::Condition::ReleaseComponent; break;
							case 'n': condition.type = PinEntry::Condition::ReleaseCodename; break;
							case 'o': condition.type = PinEntry::Condition::ReleaseVendor; break;
							case 'l': condition.type = PinEntry::Condition::ReleaseLabel; break;
							default:
								fatal2(__("invalid condition type '%c' (should be one of 'a', 'v', 'c', 'n', 'o', 'l')"),
										subExpressionType);
						}
						condition.value = stringToRegex(pinStringToRegexString(m[2]));
						pinEntry.conditions.push_back(std::move(condition));
					}
				}
				else if (pinType == "version")
				{
					PinEntry::Condition condition;
					condition.type = PinEntry::Condition::Version;
					condition.value = stringToRegex(pinStringToRegexString(pinExpression));
					pinEntry.conditions.push_back(condition);
				}
				else if (pinType == "origin")
				{
					PinEntry::Condition condition;
					condition.type = PinEntry::Condition::HostName;
					if (pinExpression.size() >= 2 && *pinExpression.begin() == '"' && *pinExpression.rbegin() == '"')
					{
						pinExpression = pinExpression.substr(1, pinExpression.size() - 2); // trimming quotes
					}
					condition.value = stringToRegex(pinStringToRegexString(pinExpression));
					pinEntry.conditions.push_back(condition);
				}
				else
				{
					fatal2(__("invalid pin type '%s' (should be one of 'release', 'version', 'origin')"), pinType);
				}
			}

			{ // processing third line
				getNextLine();
				if (file.eof())
				{
					fatal2(__("no priority line"));
				}

				static const sregex priorityRegex = sregex::compile("Pin-Priority: (.*)");
				if (!regex_match(line, m, priorityRegex))
				{
					fatal2(__("invalid priority line"));
				}

				try
				{
					pinEntry.priority = lexical_cast< ssize_t >(string(m[1]));
				}
				catch (boost::bad_lexical_cast&)
				{
					fatal2(__("invalid integer '%s'"), string(m[1]));
				}
			}

			// adding to storage
			settings.push_back(std::move(pinEntry));
		}
	}
	catch (Exception&)
	{
		fatal2(__("(at the file '%s', line %u)"), path, lineNumber);
	}
}

string getHostNameInAptPreferencesStyle(const string& baseUri)
{
	if (baseUri.empty())
	{
		return "<installed>";
	}
	else
	{
		const download::Uri uri(baseUri);
		if (uri.getProtocol() == "file" || uri.getProtocol() == "copy")
		{
			return ""; // "local site"
		}
		else
		{
			return uri.getHost();
		}
	}
}

void PinInfo::adjustUsingPinSettings(const Version* version, ssize_t& priority) const
{
	smatch m;

	FORIT(pinEntryIt, settings)
	{
		const vector< PinEntry::Condition >& conditions = pinEntryIt->conditions;
		bool matched = true;
		FORIT(conditionIt, conditions)
		{
			const PinEntry::Condition& condition = *conditionIt;
			const shared_ptr< sregex >& regex = condition.value;

			switch (condition.type)
			{
				case PinEntry::Condition::PackageName:
					matched = regex_search(version->packageName, m, *regex);
					break;
				case PinEntry::Condition::SourcePackageName:
					{
						auto binaryVersion = dynamic_cast< const BinaryVersion* >(version);
						if (!binaryVersion)
						{
							matched = false;
							break;
						}
						matched = regex_search(binaryVersion->sourcePackageName, m, *regex);
					}
					break;
				case PinEntry::Condition::Version:
					matched = regex_search(version->versionString, m, *regex);
					break;
#define RELEASE_CASE(constant, expression) \
				case PinEntry::Condition::constant: \
					matched = false; \
					FORIT(sourceIt, version->sources) \
					{ \
						const ReleaseInfo* release = sourceIt->release; \
						if (regex_search(expression, m, *regex)) \
						{ \
							matched = true; \
							break; \
						} \
					} \
					break;

				RELEASE_CASE(HostName, getHostNameInAptPreferencesStyle(release->baseUri))
				RELEASE_CASE(ReleaseArchive, release->archive)
				RELEASE_CASE(ReleaseVendor, release->vendor)
				RELEASE_CASE(ReleaseVersion, release->version)
				RELEASE_CASE(ReleaseComponent, release->component)
				RELEASE_CASE(ReleaseCodename, release->codename)
				RELEASE_CASE(ReleaseLabel, release->label)
#undef RELEASE_CASE
			}
			if (!matched)
			{
				break;
			}
		}

		if (matched)
		{
			// yeah, all conditions satisfied here, and we can set less pin too here
			priority = pinEntryIt->priority;
			break;
		}
	}
}

void PinInfo::init()
{
	smatch m;
	try
	{
		string partsDir = config->getPath("dir::etc::preferencesparts");
		vector< string > paths = fs::glob(partsDir + "/*");

		{ // filtering
			vector< string > filteredPaths;
			FORIT(pathIt, paths)
			{
				const string& path = *pathIt;
				auto name = fs::filename(path);

				static sregex preferencesNameRegex = sregex::compile("[A-Za-z0-9_.-]+");
				if (!regex_match(name, m, preferencesNameRegex))
				{
					continue;
				}

				if (name.find('.') != string::npos)
				{
					// there is an extension, then it should be 'pref'
					static sregex prefRegex = sregex::compile("\\.pref$");
					if (!regex_search(name, m, prefRegex))
					{
						continue;
					}
				}

				filteredPaths.push_back(path);
			}

			paths.swap(filteredPaths);
		}

		string mainFilePath = config->getPath("dir::etc::preferences");
		if (fs::fileExists(mainFilePath))
		{
			paths.push_back(mainFilePath);
		}

		FORIT(pathIt, paths)
		{
			loadData(*pathIt);
		}
	}
	catch (Exception&)
	{
		fatal2(__("unable to parse preferences"));
	}
}

}
}

