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

#include <iostream>
using std::cout;
using std::endl;
#include <set>
using std::set;
#include <unordered_map>
using std::unordered_map;
#include <queue>
using std::queue;
#include <stack>
using std::stack;

#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/sourcepackage.hpp>
#include <cupt/cache/sourceversion.hpp>
#include <cupt/cache/releaseinfo.hpp>
#include <cupt/system/state.hpp>

#include "../common.hpp"
#include "../handlers.hpp"
#include "../misc.hpp"
#include "../selectors.hpp"

auto printTag = [&cout](const string& first, const string& second)
{
	if (!second.empty())
	{
		cout << first << ": " << second << endl;
	}
};

int showBinaryVersions(Context& context)
{
	auto config = context.getConfig();
	vector< string > arguments;
	bpo::options_description options("");
	options.add_options()
		("installed-only", "")
		("with-release-info", "");

	auto variables = parseOptions(context, options, arguments);

	if (arguments.empty())
	{
		fatal2(__("no binary package expressions specified"));
	}

	if (!shellMode)
	{
		Version::parseOthers = true;
	}
	auto cache = context.getCache(/* source */ false, /* binary */ variables.count("installed-only") == 0,
			/* installed */ true);

	auto getReverseProvides = [&cache](const string& packageName) -> RelationLine
	{
		RelationLine result;
		if (!checkPackageName(packageName, false))
		{
			return result;
		}
		RelationExpression virtualRelationExpression(packageName);
		auto versions = cache->getSatisfyingVersions(virtualRelationExpression);
		FORIT(it, versions)
		{
			// we don't need versions of the same package
			auto newPackageName = (*it)->packageName;
			if (newPackageName == packageName)
			{
				continue;
			}

			auto newRelationVersionString = (*it)->versionString;
			RelationExpression newRelationExpression(newPackageName + " (= " + newRelationVersionString + ")");
			result.push_back(newRelationExpression);
		}
		return result;
	};

	auto p = printTag;
	for (size_t i = 0; i < arguments.size(); ++i)
	{
		const string& packageExpression = arguments[i];
		vector< const BinaryVersion* > versions;
		if (config->getBool("apt::cache::allversions"))
		{
			versions = selectAllBinaryVersionsWildcarded(cache, packageExpression);
		}
		else
		{
			bool foundVirtual = false;
			if (!cache->getBinaryPackage(packageExpression))
			{
				// there is no such binary package, maybe it's virtual?
				auto reverseProvides = getReverseProvides(packageExpression);
				if (!reverseProvides.empty())
				{
					p(__("Pure virtual package, provided by"), reverseProvides.toString());
					foundVirtual = true;
				}
			}
			if (!foundVirtual)
			{
				versions = selectBinaryVersionsWildcarded(cache, packageExpression);
			}
		}

		for (const auto& version: versions)
		{
			auto packageName = version->packageName;
			p(__("Package"), packageName);
			p(__("Version"), version->versionString);
			if (version->isInstalled())
			{
				auto installedInfo = cache->getSystemState()->getInstalledInfo(packageName);
				string status = __(system::State::InstalledRecord::Status::strings[installedInfo->status].c_str());
				if (installedInfo->want == system::State::InstalledRecord::Want::Hold)
				{
					status += string(" (") + __("on hold") + ")";
				}
				p(__("Status"), status);
				bool isAutoInstalled = cache->isAutomaticallyInstalled(packageName);
				p(__("Automatically installed"), isAutoInstalled ? __("yes") : __("no"));
			}
			else
			{
				p(__("Status"), __("not installed"));
			}
			p(__("Source"), version->sourcePackageName);
			if (version->sourceVersionString != version->versionString)
			{
				p(__("Source version"), version->sourceVersionString);
			}
			if (version->essential)
			{
				p(__("Essential"), __("yes"));
			}
			p(__("Priority"), __(Version::Priorities::strings[version->priority].c_str()));
			p(__("Section"), version->section);
			if (version->file.size)
			{
				p(__("Size"), humanReadableSizeString(version->file.size));
			}
			p(__("Uncompressed size"), humanReadableSizeString(version->installedSize));
			p(__("Maintainer"), version->maintainer);
			p(__("Architecture"), version->architecture);
			if (variables.count("with-release-info"))
			{
				for (size_t i = 0; i < version->sources.size(); ++i)
				{
					const Version::Source& entry = version->sources[i];
					p(__("Release"), entry.release->description);
				}
			}
			for (size_t i = 0; i < BinaryVersion::RelationTypes::Count; ++i)
			{
				p(__(BinaryVersion::RelationTypes::strings[i].c_str()), version->relations[i].toString());
			}
			p(__("Provides"), join(", ", version->provides));
			auto reverseProvides = getReverseProvides(packageName);
			p(__("Provided by"), reverseProvides.toString());
			{
				auto downloadInfo = version->getDownloadInfo();
				FORIT(downloadRecordIt, downloadInfo)
				{
					p("URI", downloadRecordIt->baseUri + '/' + downloadRecordIt->directory
							+ '/' + version->file.name);
				}
			}
			p("MD5", version->file.hashSums[HashSums::MD5]);
			p("SHA1", version->file.hashSums[HashSums::SHA1]);
			p("SHA256", version->file.hashSums[HashSums::SHA256]);
			{ // descriptions
				string shortDescription;
				string longDescription;
				auto localizedDescriptionPair = cache->getLocalizedDescriptions(version);
				if (!localizedDescriptionPair.first.empty())
				{
					shortDescription = localizedDescriptionPair.first;
					longDescription = localizedDescriptionPair.second;
				}
				else
				{
					shortDescription = version->shortDescription;
					longDescription = version->longDescription;
				}
				p(__("Description"), shortDescription);
				if (!shortDescription.empty())
				{
					cout << longDescription;
				}
			}
			p(__("Tags"), version->tags);
			if (version->others)
			{
				FORIT(it, (*(version->others)))
				{
					if (it->first == "Description-md5")
					{
						continue;
					}
					p(it->first, it->second);
				}
			}
			cout << endl;
		}
	}

	return 0;
}

int showSourceVersions(Context& context)
{
	auto config = context.getConfig();
	vector< string > arguments;
	bpo::options_description options("");
	options.add_options()
		("with-release-info", "");

	auto variables = parseOptions(context, options, arguments);

	if (arguments.empty())
	{
		fatal2(__("no source package expressions specified"));
	}

	if (!shellMode)
	{
		Version::parseOthers = true;
	}
	auto cache = context.getCache(/* source */ true, /* binary */ true, /* installed */ true);

	auto p = printTag;

	for (size_t i = 0; i < arguments.size(); ++i)
	{
		const string& packageExpression = arguments[i];
		vector< const SourceVersion* > versions;
		if (config->getBool("apt::cache::allversions"))
		{
			versions = selectAllSourceVersionsWildcarded(cache, packageExpression);
		}
		else
		{
			versions = selectSourceVersionsWildcarded(cache, packageExpression);
		}

		for (const auto& version: versions)
		{
			auto packageName = version->packageName;
			p(__("Package"), packageName);
			p(__("Binary"), join(", ", version->binaryPackageNames));
			p(__("Version"), version->versionString);
			p(__("Priority"), __(Version::Priorities::strings[version->priority].c_str()));
			p(__("Section"), version->section);
			p(__("Maintainer"), version->maintainer);
			if (!version->uploaders.empty())
			{
				p(__("Uploaders"), join(", ", version->uploaders));
			}
			p(__("Architectures"), join(" ", version->architectures));
			if (variables.count("with-release-info"))
			{
				for (size_t i = 0; i < version->sources.size(); ++i)
				{
					const Version::Source& entry = version->sources[i];
					p(__("Release"), entry.release->description);
				}
			}
			for (size_t i = 0; i < SourceVersion::RelationTypes::Count; ++i)
			{
				p(__(SourceVersion::RelationTypes::strings[i].c_str()), version->relations[i].toString());
			}
			{ // download info
				for (size_t i = 0; i < SourceVersion::FileParts::Count; ++i)
				{
					for (const Version::FileRecord& fileRecord: version->files[i])
					{
						cout << __(SourceVersion::FileParts::strings[i].c_str()) << ':' << endl;
						p(string("  ") + __("Size"), humanReadableSizeString(fileRecord.size));
						p("  MD5", fileRecord.hashSums[HashSums::MD5]);
						p("  SHA1", fileRecord.hashSums[HashSums::SHA1]);
						p("  SHA256", fileRecord.hashSums[HashSums::SHA256]);
						auto downloadInfo = version->getDownloadInfo();
						FORIT(it, downloadInfo)
						{
							p("  URI", it->baseUri + "/" + it->directory + "/" + fileRecord.name);
						}
					}
				}
			}

			if (version->others)
			{
				FORIT(it, (*(version->others)))
				{
					p(it->first, it->second);
				}
			}
			cout << endl;
		}
	}

	return 0;
}
int showRelations(Context& context, bool reverse)
{
	// turn off info parsing, we don't need it, only relations :)
	if (!shellMode)
	{
		Version::parseInfoOnly = false;
	}

	auto config = context.getConfig();

	vector< string > arguments;
	bpo::options_description options("");
	options.add_options()
		("installed-only", "")
		("with-suggests", "");

	auto variables = parseOptions(context, options, arguments);

	if (arguments.empty())
	{
		fatal2(__("no binary package expressions specified"));
	}

	if (reverse)
	{
		Cache::memoize = true;
	}

	auto cache = context.getCache(/* source */ false, /* binary */ variables.count("installed-only") == 0,
			/* installed */ true);

	queue< const BinaryVersion* > versions;
	FORIT(it, arguments)
	{
		auto selectedVersions = selectBinaryVersionsWildcarded(cache, *it);
		FORIT(selectedVersionIt, selectedVersions)
		{
			versions.push(*selectedVersionIt);
		}
	}

	vector< BinaryVersion::RelationTypes::Type > relationGroups;
	relationGroups.push_back(BinaryVersion::RelationTypes::PreDepends);
	relationGroups.push_back(BinaryVersion::RelationTypes::Depends);
	if (!config->getBool("apt::cache::important"))
	{
		relationGroups.push_back(BinaryVersion::RelationTypes::Recommends);
		if (variables.count("with-suggests"))
		{
			relationGroups.push_back(BinaryVersion::RelationTypes::Suggests);
		}
	}

	// don't output the same version more than one time
	set< const BinaryVersion* > processedVersions;

	// used only by rdepends
	unordered_map< string, set< string > > reverseDependsIndex;
	if (reverse)
	{
		for (const string& packageName: cache->getBinaryPackageNames())
		{
			auto package = cache->getBinaryPackage(packageName);
			auto versions = package->getVersions();
			for (const auto& version: versions)
			{
				FORIT(relationGroupIt, relationGroups)
				{
					const RelationLine& relationLine = version->relations[*relationGroupIt];
					FORIT(relationExpressionIt, relationLine)
					{
						auto satisfyingVersions = cache->getSatisfyingVersions(*relationExpressionIt);
						FORIT(satisfyingVersionIt, satisfyingVersions)
						{
							const string& satisfyingPackageName = (*satisfyingVersionIt)->packageName;
							reverseDependsIndex[satisfyingPackageName].insert(packageName);
						}
					}
				}
			}
		}
	}

	bool recurse = config->getBool("apt::cache::recursedepends");
	bool allVersions = config->getBool("apt::cache::allversions");

	while (!versions.empty())
	{
		auto version = versions.front();
		versions.pop();

		const string& packageName = version->packageName;
		const string& versionString = version->versionString;

		if (!processedVersions.insert(version).second)
		{
			continue;
		}

		cout << packageName << ' ' << versionString << ':' << endl;

		FORIT(relationGroupIt, relationGroups)
		{
			const string& caption = __(BinaryVersion::RelationTypes::strings[*relationGroupIt].c_str());

			if (!reverse)
			{
				// just plain normal dependencies
				for (const auto& relationExpression: version->relations[*relationGroupIt])
				{
					cout << "  " << caption << ": " << relationExpression.toString() << endl;
					if (recurse)
					{
						// insert recursive depends into queue
						auto satisfyingVersions = cache->getSatisfyingVersions(relationExpression);
						if (allVersions)
						{
							FORIT(satisfyingVersionIt, satisfyingVersions)
							{
								versions.push(*satisfyingVersionIt);
							}
						}
						else
						{
							// push the version with the maximum pin
							if (!satisfyingVersions.empty())
							{
								auto preferredVersion = satisfyingVersions[0];
								for (auto satisfyingVersionIt = satisfyingVersions.begin() + 1;
										satisfyingVersionIt != satisfyingVersions.end(); ++satisfyingVersionIt)
								{
									if (cache->getPin(*satisfyingVersionIt) > cache->getPin(preferredVersion))
									{
										preferredVersion = *satisfyingVersionIt;
									}
								}
								versions.push(preferredVersion);
							}
						}
					}
				}
			}
			else
			{
				// we have to check all reverse dependencies for this version
				auto packageCandidateNamesIt = reverseDependsIndex.find(packageName);
				if (packageCandidateNamesIt != reverseDependsIndex.end())
				{
					const set< string >& packageCandidateNames = packageCandidateNamesIt->second;
					FORIT(packageCandidateNameIt, packageCandidateNames)
					{
						auto packageCandidate = cache->getBinaryPackage(*packageCandidateNameIt);
						auto candidateVersions = packageCandidate->getVersions();

						for (const auto& candidateVersion: candidateVersions)
						{
							FORIT(relationExpressionIt, candidateVersion->relations[*relationGroupIt])
							{
								auto satisfyingVersions = cache->getSatisfyingVersions(*relationExpressionIt);
								for (const auto& satisfyingVersion: satisfyingVersions)
								{
									if (satisfyingVersion == version)
									{
										// positive result
										cout << "  " << __("Reverse-") << caption << ": "
												<< candidateVersion->packageName << ' '
												<< candidateVersion->versionString << ": "
												<< relationExpressionIt->toString() << endl;
										if (recurse)
										{
											versions.push(candidateVersion);
										}

										goto candidate;
									}
								}
							}
							candidate:
							;
						}
					}
				}
			}
		}
	}
	return 0;
}

int dumpConfig(Context& context)
{
	auto config = context.getConfig();

	vector< string > arguments;
	bpo::options_description noOptions("");

	parseOptions(context, noOptions, arguments);

	auto outputScalar = [&](const string& name)
	{
		auto value = config->getString(name);
		if (value.empty())
		{
			return;
		}
		cout << name << " \"" << value << "\";" << endl;
	};

	auto outputList = [&](const string& name)
	{
		cout << name << " {};" << endl;
		auto values = config->getList(name);
		FORIT(valueIt, values)
		{
			cout << name << " { \"" << *valueIt << "\"; };" << endl;
		}
	};

	checkNoExtraArguments(arguments);

	auto scalarNames = config->getScalarOptionNames();
	FORIT(nameIt, scalarNames)
	{
		outputScalar(*nameIt);
	}

	auto listNames = config->getListOptionNames();
	FORIT(nameIt, listNames)
	{
		outputList(*nameIt);
	}
	return 0;
}

int policy(Context& context, bool source)
{
	auto config = context.getConfig();

	// turn off info and relations parsing, we don't need it
	if (!shellMode)
	{
		Version::parseInfoOnly = false;
		Version::parseRelations = false;
	}

	vector< string > arguments;
	bpo::options_description options("");
	options.add_options()
		("show-dates", "");

	auto variables = parseOptions(context, options, arguments);
	if (!arguments.empty() && variables.count("show-dates"))
	{
		fatal2(__("the option '--show-dates' can be used only with no package names supplied"));
	}

	auto cache = context.getCache(/* source */ source, /* binary */ !source,
			/* installed */ !source);

	if (!arguments.empty())
	{
		// print release info for supplied package names

		FORIT(packageNameIt, arguments)
		{
			const string& packageName = *packageNameIt;
			const Package* package = (!source ?
					(const Package*)getBinaryPackage(cache, packageName) :
					(const Package*)getSourcePackage(cache, packageName));
			auto policyVersion = cache->getPolicyVersion(package);
			if (!policyVersion)
			{
				fatal2(__("no versions available for the package '%s'"), packageName);
			}

			cout << packageName << ':' << endl;

			string installedVersionString;
			if (!source)
			{
				auto binaryPackage = dynamic_cast< const BinaryPackage* >(package);
				if (!binaryPackage)
				{
					fatal2i("binary package expected");
				}
				auto installedVersion = binaryPackage->getInstalledVersion();
				if (installedVersion)
				{
					installedVersionString = installedVersion->versionString;
				}

				cout << "  " << __("Installed") << ": "
						<< (installedVersionString.empty() ? __("<none>") : installedVersionString)
						<< endl;
			}

			cout << "  " << __("Preferred") << ": " << policyVersion->versionString << endl;
			cout << "  " << __("Version table") << ':' << endl;

			auto pinnedVersions = cache->getSortedPinnedVersions(package);

			FORIT(pinnedVersionIt, pinnedVersions)
			{
				const auto& version = pinnedVersionIt->version;
				auto pin = pinnedVersionIt->pin;

				if (version->versionString == installedVersionString)
				{
					cout << " *** ";
				}
				else
				{
					cout << "     ";
				}

				cout << version->versionString << ' ' << pin << endl;

				FORIT(sourceIt, version->sources)
				{
					const shared_ptr< const ReleaseInfo > release = sourceIt->release;
					static const string spaces(8, ' ');
					cout << spaces;
					auto origin = release->baseUri;
					if (origin.empty())
					{
						origin = config->getPath("dir::state::status");
					}
					cout << origin << ' ' << release->archive << '/' << release->component << ' '
							<< '(' << (release->verified ? __("signed") : __("unsigned")) << ')' << endl;
				}
			}
		}
	}
	else
	{
		auto showDates = variables.count("show-dates");
		auto sayReleaseInfo = [&config, &showDates](const shared_ptr< const ReleaseInfo >& releaseInfo)
		{
			string origin = releaseInfo->baseUri;
			if (origin.empty())
			{
				origin = config->getPath("dir::state::status");
			}
			const string& archive = releaseInfo->archive;
			const string& component = releaseInfo->component;
			cout << "  " << origin << ' ' << archive << '/' << component << ": ";
			cout << "o=" << releaseInfo->vendor;
			cout << ",a=" << archive;
			cout << ",l=" << releaseInfo->label;
			cout << ",c=" << component;
			cout << ",v=" << releaseInfo->version;
			cout << ",n=" << releaseInfo->codename;
			if (showDates && !releaseInfo->baseUri.empty())
			{
				cout << format2(" (%s: %s, ", __("published"), releaseInfo->date);
				if (releaseInfo->validUntilDate.empty())
				{
					cout << __("does not expire");
				}
				else
				{
					cout << format2("%s: %s", __("expires"), releaseInfo->validUntilDate);
				}
				cout << ")";
			}
			cout << endl;
		};

		vector< shared_ptr< const ReleaseInfo > > data;
		if (!source)
		{
			cout << "Package files:" << endl;
			data = cache->getBinaryReleaseData();
		}
		else
		{
			cout << "Source files:" << endl;
			data = cache->getSourceReleaseData();
		}
		FORIT(releaseInfoIt, data)
		{
			sayReleaseInfo(*releaseInfoIt);
		}
	}

	return 0;
}

int showPackageNames(Context& context)
{
	auto config = context.getConfig();

	vector< string > arguments;

	bpo::options_description options("");
	options.add_options()
		("installed-only", "");
	auto variables = parseOptions(context, options, arguments);

	auto cache = context.getCache(/* source */ false, /* binary */ variables.count("installed-only") == 0,
			/* installed */ true);

	string prefix;
	if (!arguments.empty())
	{
		prefix = arguments[0];
		arguments.erase(arguments.begin());
	}
	auto prefixSize = prefix.size();

	checkNoExtraArguments(arguments);

	auto binaryPackageNames = cache->getBinaryPackageNames();
	FORIT(packageNameIt, binaryPackageNames)
	{
		// check package name for pattern and output it
		if (!packageNameIt->compare(0, prefixSize, prefix))
		{
			cout << *packageNameIt << endl;
		}
	}

	return 0;
}

int findDependencyChain(Context& context)
{
	// turn off info parsing, we don't need it, only relations
	if(!shellMode)
	{
		Version::parseInfoOnly = false;
	}

	vector< string > arguments;

	bpo::options_description options("");
	options.add_options()
		("installed-only", "");
	auto variables = parseOptions(context, options, arguments);

	if (arguments.empty())
	{
		fatal2(__("no binary package expressions specified"));
	}

	bool installedOnly = variables.count("installed-only") || (arguments.size() == 1);

	auto cache = context.getCache(/* source */ false, /* binary */ !installedOnly,
			/* installed */ true);

	auto leafPackageExpression = *(arguments.rbegin());
	arguments.erase(arguments.end() - 1);
	auto leafVersion = selectBinaryVersion(cache, leafPackageExpression, true);

	queue< const BinaryVersion* > versions;
	struct PathEntry
	{
		const BinaryVersion* version;
		BinaryVersion::RelationTypes::Type dependencyType;
		const RelationExpression* relationExpressionPtr;
	};
	map< const BinaryVersion*, PathEntry  > links;

	auto addStartingVersion = [&versions, &links](const BinaryVersion* version)
	{
		versions.push(version);
		links[version]; // create empty PathEntry for version
	};

	if (!arguments.empty())
	{
		// selected packages
		FORIT(argumentIt, arguments)
		{
			auto selectedVersions = selectBinaryVersionsWildcarded(cache, *argumentIt);
			FORIT(it, selectedVersions)
			{
				addStartingVersion(*it);
			}
		}
	}
	else
	{
		// the whole system
		auto installedVersions = cache->getInstalledVersions();
		for (const auto& installedVersion: installedVersions)
		{
			if (!cache->isAutomaticallyInstalled(installedVersion->packageName))
			{
				addStartingVersion(installedVersion);
			}
		}
	}

	auto config = context.getConfig();

	vector< BinaryVersion::RelationTypes::Type > relationGroups;
	relationGroups.push_back(BinaryVersion::RelationTypes::PreDepends);
	relationGroups.push_back(BinaryVersion::RelationTypes::Depends);
	if (config->getBool("cupt::resolver::keep-recommends"))
	{
		relationGroups.push_back(BinaryVersion::RelationTypes::Recommends);
	}
	if (config->getBool("cupt::resolver::keep-suggests"))
	{
		relationGroups.push_back(BinaryVersion::RelationTypes::Suggests);
	}

	while (!versions.empty())
	{
		auto version = versions.front();
		versions.pop();

		if (version == leafVersion)
		{
			// we found a path, re-walk it
			stack< PathEntry > path;
			const BinaryVersion* currentVersion = version;

			decltype(links.find(currentVersion)) it;
			while ((it = links.find(currentVersion)), it->second.version)
			{
				const PathEntry& pathEntry = it->second;
				path.push(pathEntry);
				currentVersion = pathEntry.version;
			}
			while (!path.empty())
			{
				PathEntry pathEntry = path.top();
				path.pop();
				cout << format2("%s %s: %s: %s",
						pathEntry.version->packageName, pathEntry.version->versionString,
						__(BinaryVersion::RelationTypes::strings[pathEntry.dependencyType].c_str()),
						pathEntry.relationExpressionPtr->toString()) << endl;
			}
			break;
		}

		FORIT(dependencyTypeIt, relationGroups)
		{
			auto dependencyType = *dependencyTypeIt;

			FORIT(relationExpressionIt, version->relations[dependencyType])
			{
				// insert recursive depends into queue
				auto satisfyingVersions = cache->getSatisfyingVersions(*relationExpressionIt);
				for (const auto& newVersion: satisfyingVersions)
				{
					auto insertResult = links.insert({ newVersion, PathEntry() });
					if (insertResult.second)
					{
						// new element
						PathEntry& newPathEntry = insertResult.first->second;
						newPathEntry.version = version;
						newPathEntry.dependencyType = dependencyType;
						// the pointer is valid because .version is alive
						newPathEntry.relationExpressionPtr = &*relationExpressionIt;

						versions.push(newVersion);
					}
				}
			}
		}
	}

	return 0;
}

int showScreenshotUris(Context& context)
{
	vector< string > arguments;
	bpo::options_description noOptions("");
	parseOptions(context, noOptions, arguments);

	if (arguments.empty())
	{
		fatal2(__("no binary package names specified"));
	}

	auto cache = context.getCache(false, true, true); // binary and installed

	FORIT(argumentIt, arguments)
	{
		const string& packageName = *argumentIt;
		// check for existence
		getBinaryPackage(cache, packageName);

		cout << "http://screenshots.debian.net/package/" << packageName << endl;
	}

	return 0;
}

int tarMetadata(Context& context)
{
	vector< string > arguments;
	bpo::options_description noOptions("");
	parseOptions(context, noOptions, arguments);
	checkNoExtraArguments(arguments);

	auto config = context.getConfig();

	auto listsDirectory = config->getPath("cupt::directory::state::lists");
	vector< string > pathList = {
		config->getPath("dir::etc::main"),
		config->getPath("dir::etc::parts"),
		config->getPath("cupt::directory::configuration::main"),
		config->getPath("cupt::directory::configuration::main-parts"),
		config->getPath("dir::etc::sourcelist"),
		config->getPath("dir::etc::sourceparts"),
		config->getPath("dir::etc::preferences"),
		config->getPath("dir::etc::preferencesparts"),
		config->getPath("dir::state::extendedstates"),
		config->getPath("dir::state::status"),
		listsDirectory + "/*Release",
		listsDirectory + "/*Packages",
		listsDirectory + "/*Sources",
	};

	string tarCommand = "tar -cf -";
	FORIT(pathIt, pathList)
	{
		tarCommand += ' ';
		tarCommand += *pathIt;
	}

	return ::system(tarCommand.c_str());
}

int showAutoInstalled(Context& context)
{
	vector< string > arguments;
	bpo::options_description options;
	options.add_options()("invert", "");
	auto variables = parseOptions(context, options, arguments);
	checkNoExtraArguments(arguments);

	bool showManual = variables.count("invert");

	auto cache = context.getCache(false, false, true); // installed only

	auto installedPackageNames = cache->getBinaryPackageNames();
	FORIT(packageNameIt, installedPackageNames)
	{
		const string& packageName = *packageNameIt;

		bool isAutoInstalled = cache->isAutomaticallyInstalled(packageName);
		if (isAutoInstalled == !showManual)
		{
			cout << packageName << endl;
		}
	}
	return 0;
}

