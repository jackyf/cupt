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
#include <ctime>

#include <common/regex.hpp>

#include <cupt/config.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/sourcepackage.hpp>
#include <cupt/cache/version.hpp>
#include <cupt/cache/binaryversion.hpp>
#include <cupt/cache/relation.hpp>
#include <cupt/cache/releaseinfo.hpp>
#include <cupt/system/state.hpp>
#include <cupt/file.hpp>

#include <internal/cacheimpl.hpp>
#include <internal/filesystem.hpp>
#include <internal/pininfo.hpp>
#include <internal/tagparser.hpp>
#include <internal/regex.hpp>
#include <internal/common.hpp>
#include <internal/cachefiles.hpp>

namespace cupt {
namespace internal {

void CacheImpl::processProvides(const string* packageNamePtr,
		const char* providesStringStart, const char* providesStringEnd)
{
	auto callback = [this, &packageNamePtr](const char* tokenBeginIt, const char* tokenEndIt)
	{
		this->canProvide[string(tokenBeginIt, tokenEndIt)].insert(packageNamePtr);
	};
	processSpaceCommaSpaceDelimitedStrings(
			providesStringStart, providesStringEnd, callback);
}

shared_ptr< Package > CacheImpl::newBinaryPackage(const string& packageName) const
{
	bool needsReinstall = false;
	smatch m;
	FORIT(regexPtrIt, packageNameRegexesToReinstall)
	{
		if (regex_search(packageName, m, **regexPtrIt))
		{
			needsReinstall = true;
			break;
		}
	}

	return shared_ptr< Package >(new BinaryPackage(binaryArchitecture, needsReinstall));
}

shared_ptr< Package > CacheImpl::newSourcePackage(const string& /* packageName */) const
{
	return shared_ptr< Package >(new SourcePackage(binaryArchitecture));
}

shared_ptr< Package > CacheImpl::preparePackage(unordered_map< string, vector< PrePackageRecord > >& pre,
		unordered_map< string, shared_ptr< Package > >& target, const string& packageName,
		decltype(&CacheImpl::newBinaryPackage) packageBuilderMethod) const
{
	auto preIt = pre.find(packageName);
	if (preIt != pre.end())
	{
		shared_ptr< Package >& package = target[packageName];
		if (!package)
		{
			// there was no such package before, and an insertion occured
			// so, we need to create the package
			package = (this->*packageBuilderMethod)(packageName);
		}
		vector< PrePackageRecord >& preRecord = preIt->second;
		FORIT(preRecordIt, preRecord)
		{
			Version::InitializationParameters versionInitParams;
			versionInitParams.releaseInfo = preRecordIt->releaseInfoAndFile->first;
			versionInitParams.file = preRecordIt->releaseInfoAndFile->second;
			versionInitParams.offset = preRecordIt->offset;
			versionInitParams.packageName = packageName;
			package->addEntry(versionInitParams);
		}
		preRecord.clear();
		return package;
	}
	else
	{
		return shared_ptr< Package >();
	}
}

vector< shared_ptr< const BinaryVersion > >
CacheImpl::getSatisfyingVersions(const Relation& relation) const
{
	vector< shared_ptr< const BinaryVersion > > result;

	const string& packageName = relation.packageName;

	auto package = getBinaryPackage(packageName);

	if (package)
	{
		// if such binary package exists
		auto versions = package->getVersions();
		FORIT(it, versions)
		{
			if (relation.isSatisfiedBy((*it)->versionString))
			{
				result.push_back(*it);
			}
		}
	}

	// virtual package can only be considered if no relation sign is specified
	if (relation.relationType == Relation::Types::None)
	{
		// looking for reverse-provides
		auto reverseProvidesIt = canProvide.find(packageName);
		if (reverseProvidesIt != canProvide.end())
		{
			const set< const string* >& reverseProvides = reverseProvidesIt->second;
			FORIT(it, reverseProvides)
			{
				auto reverseProvidePackage = getBinaryPackage(**it);
				if (!reverseProvidePackage)
				{
					continue;
				}
				auto versions = reverseProvidePackage->getVersions();
				FORIT(versionIt, versions)
				{
					const vector< string >& realProvides = (*versionIt)->provides;
					FORIT(realProvidesIt, realProvides)
					{
						if (*realProvidesIt == packageName)
						{
							// ok, this particular version does provide this virtual package
							result.push_back(*versionIt);
							break;
						}
					}
				}
			}
		}
	}

	return result;
}

shared_ptr< const BinaryPackage > CacheImpl::getBinaryPackage(const string& packageName) const
{
	auto it = binaryPackages.find(packageName);
	if (it == binaryPackages.end())
	{
		auto prepareResult = preparePackage(preBinaryPackages,
				binaryPackages, packageName, &CacheImpl::newBinaryPackage);
		// can be empty/NULL also
		return static_pointer_cast< const BinaryPackage >(prepareResult);
	}
	else
	{
		return static_pointer_cast< const BinaryPackage >(it->second);
	}
}

shared_ptr< const SourcePackage > CacheImpl::getSourcePackage(const string& packageName) const
{
	auto it = sourcePackages.find(packageName);
	if (it == sourcePackages.end())
	{
		auto prepareResult = preparePackage(preSourcePackages,
				sourcePackages, packageName, &CacheImpl::newSourcePackage);
		// can be empty/NULL also
		return static_pointer_cast< const SourcePackage >(prepareResult);
	}
	else
	{
		return static_pointer_cast< const SourcePackage >(it->second);
	}
}

void CacheImpl::parseSourcesLists()
{
	try
	{
		string partsDir = config->getPath("dir::etc::sourceparts");
		vector< string > sourceFiles = fs::glob(partsDir + "/*.list");

		string mainFilePath = config->getPath("dir::etc::sourcelist");
		if (fs::fileExists(mainFilePath))
		{
			sourceFiles.push_back(mainFilePath);
		}

		FORIT(pathIt, sourceFiles)
		{
			parseSourceList(*pathIt);
		}
	}
	catch (Exception&)
	{
		fatal("error while parsing sources list");
	}
}

void CacheImpl::parseSourceList(const string& path)
{
	string openError;
	File file(path, "r", openError);
	if (!openError.empty())
	{
		fatal("unable to open file '%s': %s", path.c_str(), openError.c_str());
	}

	string line;
	static sregex toSkip = sregex::compile("^\\s*(?:#.*)?$");

	{
		size_t lineNumber = 0;
		while (! file.getLine(line).eof())
		{
			++lineNumber;

			// skip all empty lines and lines with comments
			if (regex_match(line, toSkip))
			{
				continue;
			}
			vector< string > tokens;
			tokens = internal::split(sregex::compile("[\\t ]+"), line);

			IndexEntry entry;

			// type
			if (tokens.empty())
			{
				fatal("undefined source type at file '%s', line %u", path.c_str(), lineNumber);
			}
			else
			{
				if (tokens[0] == "deb")
				{
					entry.category = IndexEntry::Binary;
				}
				else if (tokens[0] == "deb-src")
				{
					entry.category = IndexEntry::Source;
				}
				else
				{
					fatal("incorrect source type at file '%s', line %u", path.c_str(), lineNumber);
				}
			}

			// uri
			if (tokens.size() < 2)
			{
				fatal("undefined source uri at file '%s', line %u", path.c_str(), lineNumber);
			}
			else
			{
				entry.uri = tokens[1];
			}

			if (tokens.size() < 3)
			{
				fatal("undefined source distribution at file '%s', line %u", path.c_str(), lineNumber);
			}
			else
			{
				entry.distribution = tokens[2];
			}

			if (*entry.uri.rbegin() == '/')
			{
				entry.uri.erase(entry.uri.end() - 1); // strip last '/' if present
			}

			if (tokens.size() > 3)
			{
				// there are components (sections) specified, it's a normal entry
				for_each(tokens.begin() + 3, tokens.end(), [this, &entry](const string& component)
				{
					entry.component = component;
					indexEntries.push_back(entry);
				});
			}
			else
			{
				// this a candidate for easy entry
				// distribution must end with a slash
				if (*entry.distribution.rbegin() == '/')
				{
					entry.distribution.erase(entry.distribution.end() - 1); // strip last '/' if present
					entry.component = "";
					indexEntries.push_back(entry);
				}
				else
				{
					fatal("distribution doesn't end with a slash at file '%s', line %u", path.c_str(), lineNumber);
				}
			}
		}
	}
}

void CacheImpl::processIndexEntry(const IndexEntry& indexEntry)
{
	string indexFileToParse = cachefiles::getPathOfIndexList(*config, indexEntry);

	string indexAlias = indexEntry.uri + ' ' + indexEntry.distribution + ' ' +
			indexEntry.component + ' ' +
			((indexEntry.category == IndexEntry::Binary) ? "(binary)" : "source");

	try
	{
		auto releaseInfo = getReleaseInfo(
				cachefiles::getPathOfReleaseList(*config, indexEntry));
		releaseInfo->component = indexEntry.component;
		releaseInfo->baseUri = indexEntry.uri;
		if (indexEntry.category == IndexEntry::Binary)
		{
			binaryReleaseData.push_back(releaseInfo);
		}
		else
		{
			sourceReleaseData.push_back(releaseInfo);
		}

		processIndexFile(indexFileToParse, indexEntry.category, releaseInfo);
	}
	catch (Exception&)
	{
		warn("skipped the index '%s'", indexAlias.c_str());
	}

	try  // processing translations if any
	{
		auto descriptionTranslationPaths =
				cachefiles::getPathsOfLocalizedDescriptions(*config, indexEntry);
		FORIT(pathIt, descriptionTranslationPaths)
		{
			string errorString;
			File file(*pathIt, "r", errorString);
			if (errorString.empty())
			{
				processTranslationFile(*pathIt);
				break;
			}
		}
	}
	catch (Exception&)
	{
		warn("skipped translations of the index '%s'", indexAlias.c_str());
	}
}

shared_ptr< ReleaseInfo > CacheImpl::getReleaseInfo(const string& path) const
{
	shared_ptr< ReleaseInfo > result(new ReleaseInfo);
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
					bool warnOnly = config->getBool("cupt::cache::release-file-expiration::ignore");
					(warnOnly ? warn : fatal)("release file '%s' has expired (expiry time '%s'), discarding it",
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

	result->verified = Cache::verifySignature(config, path);

	return result;
}

void CacheImpl::processIndexFile(const string& path, IndexEntry::Type category,
		shared_ptr< const ReleaseInfo > releaseInfo)
{
	using std::make_pair;
	auto prePackagesStorage = (category == IndexEntry::Binary ?
			&preBinaryPackages : &preSourcePackages);

	string openError;
	shared_ptr< File > file(new File(path, "r", openError));
	if (!openError.empty())
	{
		fatal("unable to open index file '%s': %s", path.c_str(), openError.c_str());
	}

	releaseInfoAndFileStorage.push_back(make_pair(releaseInfo, file));
	PrePackageRecord prePackageRecord;
	prePackageRecord.releaseInfoAndFile = &*(releaseInfoAndFileStorage.rbegin());

	try
	{
		pair< const string, vector< PrePackageRecord > > pairForInsertion;
		string& packageName = const_cast< string& > (pairForInsertion.first);

		while (true)
		{
			const char* buf;
			size_t size;
			file->rawGetLine(buf, size);
			if (file->eof())
			{
				break;
			}

			static const size_t packageAnchorLength = sizeof("Package: ") - 1;
			if (size > packageAnchorLength && !memcmp("Package: ", buf, packageAnchorLength))
			{
				packageName.assign(buf + packageAnchorLength, size - packageAnchorLength - 1);
			}
			else
			{
				fatal("unable to find correct Package line");
			}

			try
			{
				checkPackageName(packageName);
			}
			catch (Exception&)
			{
				warn("discarding this package version from index file '%s'", path.c_str());
				while (file->rawGetLine(buf, size), size > 1) {}
				continue;
			}

			prePackageRecord.offset = file->tell();

			auto it = prePackagesStorage->insert(pairForInsertion).first;
			it->second.push_back(prePackageRecord);

			while (file->rawGetLine(buf, size), size > 1)
			{
				static const size_t providesAnchorLength = sizeof("Provides: ") - 1;
				if (*buf == 'P' && size > providesAnchorLength && !memcmp("rovides: ", buf+1, providesAnchorLength-1))
				{
					processProvides(&it->first, buf + providesAnchorLength, buf + size - 1);
				}
			}
		}
	}
	catch (Exception&)
	{
		fatal("error parsing index file '%s'", path.c_str());
	}
}

void CacheImpl::processTranslationFile(const string& path)
{
	string errorString;
	shared_ptr< File > file(new File(path, "r", errorString));
	if (!errorString.empty())
	{
		fatal("unable to open translation file '%s': %s", path.c_str(), errorString.c_str());
	}

	try
	{
		TagParser parser(&*file);
		TagParser::StringRange tagName, tagValue;

		static const char descriptionSubPattern[] = "Description-";
		static const size_t descriptionSubPatternSize = sizeof(descriptionSubPattern) - 1;

		size_t recordPosition;

		string md5;
		TranslationPosition translationPosition;
		translationPosition.file = file;

		while ((recordPosition = file->tell()), (parser.parseNextLine(tagName, tagValue) && !file->eof()))
		{
			bool hashSumFound = false;
			bool translationFound = false;

			do
			{
				if (tagName.equal(BUFFER_AND_SIZE("Description-md5")))
				{
					hashSumFound = true;
					md5 = tagValue;
				}
				else if ((size_t)(tagName.second - tagName.first) > descriptionSubPatternSize &&
						!memcmp(&*tagName.first, descriptionSubPattern, descriptionSubPatternSize))
				{
					translationFound = true;
					translationPosition.offset = file->tell() - (tagValue.second - tagValue.first) - 1; // -1 for '\n'
				}
			} while (parser.parseNextLine(tagName, tagValue));

			if (!hashSumFound)
			{
				fatal("unable to find md5 hash in a translation record starting at byte '%u'", recordPosition);
			}
			if (!translationFound)
			{
				fatal("unable to find translation in a translation record starting at byte '%u'", recordPosition);
			}

			translations[md5] = translationPosition;
		}
	}
	catch(Exception&)
	{
		fatal("error parsing translation file '%s'", path.c_str());
	}
}

void CacheImpl::parsePreferences()
{
	pinInfo.reset(new PinInfo(config, systemState));
}

ssize_t CacheImpl::getPin(const shared_ptr< const Version >& version, const string& installedVersionString) const
{
	if (Cache::memoize)
	{
		auto it = pinCache.find(version);
		if (it != pinCache.end())
		{
			return it->second;
		}
	}

	auto result = pinInfo->getPin(version, installedVersionString);
	if (Cache::memoize)
	{
		pinCache.insert(std::make_pair(version, result));
	}
	return result;
}

pair< string, string > CacheImpl::getLocalizedDescriptions(const shared_ptr< const BinaryVersion >& version) const
{
	string source = version->shortDescription + "\n" + version->longDescription;
	string sourceHash = HashSums::getHashOfString(HashSums::MD5, source);

	auto it = translations.find(sourceHash);
	if (it != translations.end())
	{
		const TranslationPosition& position = it->second;
		string combinedDescription;
		position.file->seek(position.offset);
		position.file->getRecord(combinedDescription);

		auto firstNewLinePosition = combinedDescription.find('\n');

		return make_pair(combinedDescription.substr(0, firstNewLinePosition),
				combinedDescription.substr(firstNewLinePosition+1));
	}
	return pair< string, string >();
}

void CacheImpl::parseExtendedStates()
{
	// we are parsing duals like:

	// Package: perl
	// Auto-Installed: 1

	// but, rarely another fields may be present, we need to ignore them

	try
	{
		string path = cachefiles::getPathOfExtendedStates(*config);

		string openError;
		File file(path, "r", openError);
		if (!openError.empty())
		{
			fatal("unable to open file '%s': %s", path.c_str(), openError.c_str());
		}

		internal::TagParser parser(&file);
		internal::TagParser::StringRange tagName, tagValue;

		while (parser.parseNextLine(tagName, tagValue) && !file.eof())
		{
			if (!tagName.equal("Package", 7))
			{
				fatal("wrong tag: expected 'Package', got '%s' at file '%s'",
						string(tagName).c_str(), path.c_str());
			}

			string packageName = tagValue;

			bool valueFound = false;
			while (parser.parseNextLine(tagName, tagValue))
			{
				if (tagName.equal(BUFFER_AND_SIZE("Auto-Installed")))
				{
					valueFound = true;
					if (tagValue.equal(BUFFER_AND_SIZE("1")))
					{
						// adding to storage
						extendedInfo.automaticallyInstalled.insert(packageName);
					}
					else if (!tagValue.equal(BUFFER_AND_SIZE("0")))
					{
						fatal("bad value '%s' (should be 0 or 1) for the package '%s' at file '%s'",
								string(tagValue).c_str(), packageName.c_str(), path.c_str());
					}
				}
			}

			if (!valueFound)
			{
				fatal("no 'Auto-Installed' tag for the package '%s' at file '%s'",
						packageName.c_str(), path.c_str());
			}
		}
	}
	catch (Exception&)
	{
		fatal("error while parsing extended states");
	}
}

vector< shared_ptr< const BinaryVersion > >
CacheImpl::getSatisfyingVersions(const RelationExpression& relationExpression) const
{
	string memoizeKey;
	if (Cache::memoize)
	{
		// caching results
		memoizeKey = relationExpression.getHashString();
		auto it = getSatisfyingVersionsCache.find(memoizeKey);
		if (it != getSatisfyingVersionsCache.end())
		{
			return it->second;
		}
	}

	auto result = getSatisfyingVersions(relationExpression[0]);

	// now, if alternatives (OR groups) are present, we should add them too,
	// without duplicates, but without sorting to not change the order, specified
	// in relation expression
	for (auto relationIt = relationExpression.begin() + 1;
		relationIt != relationExpression.end(); ++relationIt)
	{
		auto source = getSatisfyingVersions(*relationIt);
		FORIT(versionIt, source)
		{
			auto predicate = std::bind2nd(PointerEqual< const BinaryVersion >(), *versionIt);
			if (std::find_if(result.begin(), result.end(), predicate) == result.end())
			{
				result.push_back(*versionIt);
			}
		}
	}

	if (Cache::memoize)
	{
		getSatisfyingVersionsCache.insert(
				pair< const string, decltype(result) >(std::move(memoizeKey), result));
	}

	return result;
}

}
}

