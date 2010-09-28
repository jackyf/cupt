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
#include <boost/lexical_cast.hpp>

#include <cupt/cache/version.hpp>
#include <cupt/regex.hpp>
#include <cupt/file.hpp>
#include <cupt/config.hpp>
#include <cupt/cache/releaseinfo.hpp>
#include <cupt/cache/binaryversion.hpp>
#include <cupt/system/state.hpp>

#include <internal/pininfo.hpp>
#include <internal/filesystem.hpp>

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

ssize_t PinInfo::getOriginalAptPin(const shared_ptr< const Version >& version) const
{
	static const ssize_t defaultReleasePriority = 990;
	static const ssize_t notAutomaticReleasePriority = 1;
	static const ssize_t installedPriority = 100;
	static const ssize_t defaultPriority = 500;

	ssize_t result = 0;

	auto defaultRelease = config->getString("apt::default-release");

	size_t availableAsCount = version->availableAs.size();
	for (size_t i = 0; i < availableAsCount; ++i)
	{
		const Version::AvailableAsEntry& entry = version->availableAs[i];
		auto currentPriority = defaultPriority;
		if (!defaultRelease.empty() &&
			(entry.release->archive == defaultRelease || entry.release->codename == defaultRelease))
		{
			currentPriority = defaultReleasePriority;
		}
		else if (entry.release->notAutomatic)
		{
			currentPriority = notAutomaticReleasePriority;
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

ssize_t PinInfo::getPin(const shared_ptr< const Version >& version,
		const string& installedVersionString) const
{
	auto result = getOriginalAptPin(version);

	// discourage downgrading
	// downgradings will usually have pin <= 0
	if (!installedVersionString.empty())
	{
		auto installedInfo = systemState->getInstalledInfo(version->packageName);
		if (!installedInfo)
		{
			fatal("internal error: missing installed info for package '%s'", version->packageName.c_str());
		}

		if (compareVersionStrings(installedVersionString, version->versionString) > 0)
		{
			result -= 2000;
		}

		auto binaryVersion = dynamic_pointer_cast< const BinaryVersion >(version);
		if (!binaryVersion)
		{
			fatal("internal error: version is not binary");
		}
		if (installedInfo->want == system::State::InstalledRecord::Want::Hold && binaryVersion->isInstalled())
		{
			result += config->getNumber("cupt::cache::obey-hold");
		}
	}

	if (version->isVerified())
	{
		result += 1;
	}

	return result;
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
		fatal("unable to open file '%s': %s", path.c_str(), openError.c_str());
	}

	string line;
	smatch m;
	size_t lineNumber = 0;
	while (!file.getLine(line).eof())
	{
		++lineNumber;

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
				fatal("invalid package/source line at file '%s', line %u", path.c_str(), lineNumber);
			}

			condition.type = (string(m[1]) == "Package" ?
					PinEntry::Condition::PackageName : PinEntry::Condition::SourcePackageName);

			vector< string > parts = split(' ', m[2]);
			FORIT(it, parts)
			{
				*it = globToRegexString(*it);
			}
			condition.value = stringToRegex(join("|", parts));
			pinEntry.conditions.push_back(std::move(condition));
		}

		{ // processing second line
			file.getLine(line);
			if (file.eof())
			{
				fatal("no pin line at file '%s' line %u", path.c_str(), lineNumber);
			}

			static const sregex pinRegex = sregex::compile("Pin: (\\w+?) (.*)");
			if (!regex_match(line, m, pinRegex))
			{
				fatal("invalid pin line at file '%s' line %u", path.c_str(), lineNumber);
			}

			string pinType = m[1];
			string pinExpression = m[2];
			if (pinType == "release")
			{
				static const sregex commaSeparatedRegex = sregex::compile("\\s*,\\s*");
				auto subExpressions = split(commaSeparatedRegex, pinExpression);

				FORIT(subExpressionIt, subExpressions)
				{
					PinEntry::Condition condition;

					static const sregex subExpressionRegex = sregex::compile("(\\w)=(.*)");
					if (!regex_match(*subExpressionIt, m, subExpressionRegex))
					{
						fatal("invalid condition '%s' in release expression at file '%s' line %u",
								subExpressionIt->c_str(), path.c_str(), lineNumber);
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
							fatal("invalid condition type '%c' (should be one of 'a', 'v', 'c', 'n', 'o', 'l') "
									"in release expression at file '%s' line %u",
									subExpressionType, path.c_str(), lineNumber);
					}
					condition.value = globToRegex(m[2]);
					pinEntry.conditions.push_back(std::move(condition));
				}
			}
			else if (pinType == "version")
			{
				PinEntry::Condition condition;
				condition.type = PinEntry::Condition::Version;
				condition.value = globToRegex(pinExpression);
				pinEntry.conditions.push_back(condition);
			}
			else if (pinType == "origin")
			{
				PinEntry::Condition condition;
				condition.type = PinEntry::Condition::BaseUri;
				condition.value = globToRegex(pinExpression);
				pinEntry.conditions.push_back(condition);
			}
			else
			{
				fatal("invalid pin type '%s' (should be one of 'release', 'version', 'origin') "
						"at file '%s' line %u", pinType.c_str(), path.c_str(), lineNumber);
			}
		}

		{ // processing third line
			file.getLine(line);
			if (file.eof())
			{
				fatal("no priority line at file '%s' line %u", path.c_str(), lineNumber);
			}

			static const sregex priorityRegex = sregex::compile("Pin-Priority: (.*)");
			if (!regex_match(line, m, priorityRegex))
			{
				fatal("invalid priority line at file '%s' line %u", path.c_str(), lineNumber);
			}

			pinEntry.priority = lexical_cast< ssize_t >(string(m[1]));
		}

		// adding to storage
		settings.push_back(std::move(pinEntry));
	}
}

void PinInfo::adjustUsingPinSettings(const shared_ptr< const Version >& version, ssize_t& priority) const
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
					matched = regex_match(version->packageName, m, *regex);
					break;
				case PinEntry::Condition::SourcePackageName:
					{
						auto binaryVersion = dynamic_pointer_cast< const BinaryVersion >(version);
						if (!binaryVersion)
						{
							matched = false;
							break;
						}
						matched = regex_match(binaryVersion->sourcePackageName, m, *regex);
					}
					break;
				case PinEntry::Condition::Version:
					matched = regex_match(version->versionString, m, *regex);
					break;
#define RELEASE_CASE(constant, member) \
				case PinEntry::Condition::constant: \
					matched = false; \
					FORIT(availableAsRecordIt, version->availableAs) \
					{ \
						const shared_ptr< const ReleaseInfo >& release = availableAsRecordIt->release; \
						if (regex_match(release->member, m, *regex)) \
						{ \
							matched = true; \
							break; \
						} \
					} \
					break;

				RELEASE_CASE(BaseUri, baseUri)
				RELEASE_CASE(ReleaseArchive, archive)
				RELEASE_CASE(ReleaseVendor, vendor)
				RELEASE_CASE(ReleaseVersion, version)
				RELEASE_CASE(ReleaseComponent, component)
				RELEASE_CASE(ReleaseCodename, codename)
				RELEASE_CASE(ReleaseLabel, label)
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
		string fullEtcDir = config->getString("dir") + config->getString("dir::etc");
		string partsDir = config->getString("dir::etc::preferencesparts");
		vector< string > paths = fs::glob(fullEtcDir + "/" + partsDir + "/*");

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

		string mainFilePath = fullEtcDir + "/" + config->getString("dir::etc::preferences");
		if (fs::exists(mainFilePath))
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
		fatal("error while parsing preferences");
	}
}

}
}

