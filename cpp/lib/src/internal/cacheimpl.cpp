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
#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/sourcepackage.hpp>
#include <cupt/cache/version.hpp>
#include <cupt/cache/binaryversion.hpp>
#include <cupt/cache/relation.hpp>
#include <cupt/cache/releaseinfo.hpp>
#include <cupt/system/state.hpp>
#include <cupt/file.hpp>
#include <cupt/versionstring.hpp>
#include <cupt/packagename.hpp>

#include <internal/cacheimpl.hpp>
#include <internal/filesystem.hpp>
#include <internal/pininfo.hpp>
#include <internal/tagparser.hpp>
#include <internal/regex.hpp>
#include <internal/parse.hpp>
#include <internal/cachefiles.hpp>
#include <internal/indexofindex.hpp>
#include <internal/versionparse.hpp>

namespace cupt {
namespace internal {

CacheImpl::CacheImpl()
	: __smatch_ptr(new smatch)
{}

CacheImpl::~CacheImpl()
{
	delete __smatch_ptr;
}

void CacheImpl::processProvides(const string* packageNamePtr,
		const char* providesStringStart, const char* providesStringEnd)
{
	auto callback = [this, &packageNamePtr](const char* tokenBeginIt, const char* tokenEndIt)
	{
		auto& sublist = this->canProvide[string(tokenBeginIt, tokenEndIt)];
		if (std::find(sublist.begin(), sublist.end(), packageNamePtr) == sublist.end())
		{
			sublist.push_back(packageNamePtr);
		}
	};
	parse::processSpaceCharSpaceDelimitedStrings(
			providesStringStart, providesStringEnd, ',', callback);
}

Package* CacheImpl::newBinaryPackage() const
{
	return new BinaryPackage();
}

Package* CacheImpl::newSourcePackage() const
{
	return new SourcePackage();
}

Package* CacheImpl::preparePackage(unordered_map< string, vector< PrePackageRecord > >& pre,
		unordered_map< string, unique_ptr< Package > >& target, const string& packageName,
		decltype(&CacheImpl::newBinaryPackage) packageBuilderMethod) const
{
	auto targetIt = target.find(packageName);
	if (targetIt != target.end())
	{
		return targetIt->second.get();
	}

	auto preIt = pre.find(packageName);
	if (preIt != pre.end())
	{
		auto& package = target[packageName];
		package.reset( (this->*packageBuilderMethod)() );

		internal::VersionParseParameters versionInitParams;
		versionInitParams.packageNamePtr = &packageName;
		versionInitParams.binaryArchitecturePtr = binaryArchitecture.get();

		vector< PrePackageRecord >& preRecord = preIt->second;
		FORIT(preRecordIt, preRecord)
		{
			versionInitParams.releaseInfo = preRecordIt->releaseInfoAndFile->first.get();
			versionInitParams.file = preRecordIt->releaseInfoAndFile->second.get();
			versionInitParams.offset = preRecordIt->offset;
			package->addEntry(versionInitParams);
		}
		preRecord.clear();
		return package.get();
	}
	else
	{
		return nullptr;
	}
}

template < bool multiarchAllowedRequired >
static inline void addSatisfyingPackageVersions(
		vector<const BinaryVersion*>* result, const Relation& relation,
		const BinaryPackage* package, const system::State& systemState)
{
	if (package)
	{
		// if such binary package exists
		for (auto version: *package)
		{
			if (relation.isSatisfiedBy(version->versionString))
			{
				if (version->isInstalled() &&
						systemState.getInstalledInfo(version->packageName)->isBroken() &&
						relation.relationType != Relation::Types::LiteralyEqual)
				{
					continue;
				}
				if (multiarchAllowedRequired && version->multiarch != "allowed")
				{
					continue;
				}

				result->push_back(version);
			}
		}
	}
}

void CacheImpl::addRealPackageSatisfyingVersions(vector<const BinaryVersion*>* result, const Relation& relation) const
{
	auto package = getBinaryPackage(relation.packageName);

	if (relation.architecture.empty())
	{
		addSatisfyingPackageVersions<false>(result, relation, package, *systemState);
	}
	else if (relation.architecture.compare(0, string::npos, "any", 3) == 0)
	{
		addSatisfyingPackageVersions<true>(result, relation, package, *systemState);
	}
	// otherwise unsupported
}

namespace {

bool providesChunkMatchesRelation(const Relation& providesChunk, const Relation& relation)
{
	if (providesChunk.packageName != relation.packageName)
	{
		return false;
	}
	if (relation.relationType == Relation::Types::None)
	{
		return true;
	}
	if (providesChunk.relationType != Relation::Types::Equal)
	{
		return false;
	}
	return relation.isSatisfiedBy(providesChunk.versionString);
}

}

void CacheImpl::addVirtualPackageSatisfyingVersions(vector<const BinaryVersion*>* result, const Relation& relation) const
{
	auto reverseProvidesIt = canProvide.find(relation.packageName);
	if (reverseProvidesIt != canProvide.end())
	{
		for (const auto& it: reverseProvidesIt->second)
		{
			auto reverseProvidePackage = getBinaryPackage(*it);
			if (!reverseProvidePackage)
			{
				continue;
			}
			for (auto version: *reverseProvidePackage)
			{
				if (version->isInstalled() &&
						systemState->getInstalledInfo(version->packageName)->isBroken())
				{
					continue;
				}
				for (const auto& providesChunk: version->provides)
				{
					if (providesChunkMatchesRelation(providesChunk, relation))
					{
						// ok, this particular version does provide this virtual package
						result->push_back(version);
						break;
					}
				}
			}
		}
	}
}

static inline void sortByPackageNameAndVersion(vector<const BinaryVersion*>* result)
{
	std::sort(result->begin(), result->end(), [](const BinaryVersion* left, const BinaryVersion* right)
			{
				return std::forward_as_tuple(left->packageName, right->versionString) <
						std::forward_as_tuple(right->packageName, left->versionString);
			});
}

vector< const BinaryVersion* >
CacheImpl::getSatisfyingVersionsNonCached(const Relation& relation) const
{
	vector< const BinaryVersion* > result;

	addRealPackageSatisfyingVersions(&result, relation);
	addVirtualPackageSatisfyingVersions(&result, relation);
	sortByPackageNameAndVersion(&result);

	return result;
}

const BinaryPackage* CacheImpl::getBinaryPackage(const string& packageName) const
{
	return static_cast< const BinaryPackage* >(preparePackage(
			preBinaryPackages, binaryPackages, packageName, &CacheImpl::newBinaryPackage));
}

const SourcePackage* CacheImpl::getSourcePackage(const string& packageName) const
{
	return static_cast< const SourcePackage* >(preparePackage(
			preSourcePackages, sourcePackages, packageName, &CacheImpl::newSourcePackage));
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
		fatal2(__("unable to parse the sources list"));
	}
}

void stripComment(string& s)
{
	auto commentPosition = s.find('#');
	if (commentPosition != string::npos)
	{
		s.erase(commentPosition);
	}
}

static void parseOutKeyValueOptions(vector< string >& tokens, Cache::IndexEntry* entry)
{
	if (tokens.size() < 2) return;
	auto openingBracketTokenIt = tokens.begin() + 1;
	if (*openingBracketTokenIt != "[") return;

	auto closingBracketTokenIt = std::find(openingBracketTokenIt+1, tokens.end(), "]");
	if (closingBracketTokenIt == tokens.end())
	{
		fatal2(__("no closing token (']') for options"));
	}

	for (auto it = openingBracketTokenIt+1; it != closingBracketTokenIt; ++it)
	{
		const string& token = *it;
		auto keyValueDelimiterPosition = token.find('=');
		if (keyValueDelimiterPosition == string::npos)
		{
			fatal2(__("no key-value separator ('=') in the option token '%s'"), token);
		}
		entry->options[token.substr(0, keyValueDelimiterPosition)] = token.substr(keyValueDelimiterPosition+1);
	}

	tokens.erase(openingBracketTokenIt, closingBracketTokenIt+1);
}

static void parseSourceListLine(const string& line, vector< Cache::IndexEntry >* indexEntries)
{
	typedef Cache::IndexEntry IndexEntry;

	vector< string > tokens;
	static const sregex tokenRegex = sregex::compile("[\\t ]+");
	tokens = internal::split(tokenRegex, line);

	IndexEntry entry;

	// type
	if (tokens.empty())
	{
		fatal2(__("undefined source type"));
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
			fatal2(__("incorrect source type"));
		}
	}

	parseOutKeyValueOptions(tokens, &entry);

	// uri
	if (tokens.size() < 2)
	{
		fatal2(__("undefined source uri"));
	}
	else
	{
		entry.uri = tokens[1];
	}

	if (tokens.size() < 3)
	{
		fatal2(__("undefined source distribution"));
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
		for_each(tokens.begin() + 3, tokens.end(), [&indexEntries, &entry](const string& component)
		{
			entry.component = component;
			indexEntries->push_back(entry);
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
			indexEntries->push_back(entry);
		}
		else
		{
			fatal2(__("distribution doesn't end with a slash"));
		}
	}
}

void CacheImpl::parseSourceList(const string& path)
{
	RequiredFile file(path, "r");

	string line;
	static sregex toSkip = sregex::compile("^\\s*(?:#.*)?$");
	size_t lineNumber = 0;

	try
	{
		while (! file.getLine(line).eof())
		{
			++lineNumber;

			// skip all empty lines and lines with comments
			if (regex_match(line, toSkip))
			{
				continue;
			}
			stripComment(line);

			parseSourceListLine(line, &indexEntries);
		}
	}
	catch (Exception&)
	{
		fatal2(__("(at the file '%s', line %u)"), path, lineNumber);
	}
}

class ReleaseLimits
{
	struct Item
	{
		std::function< string (const ReleaseInfo&) > attributeExtractor;
		bool typeIsInclude;
		vector< string > values;
	};
	std::list< Item > __items;
 public:
	ReleaseLimits(const Config& config)
	{
		const static map< string, std::function< string (const ReleaseInfo&) > > limitCategories = {
			{ "archive", [](const ReleaseInfo& ri) { return ri.archive; } },
			{ "codename", [](const ReleaseInfo& ri) { return ri.codename; } }
		};

		FORIT(categoryIt, limitCategories)
		{
			auto limitValuesOptionName = string("cupt::cache::limit-releases::by-") + categoryIt->first;
			auto limitTypeOptionName = limitValuesOptionName + "::type";

			auto limitType = config.getString(limitTypeOptionName);
			bool limitTypeIsInclude = true /* will be re-initialized anyway */;
			if (limitType == "none")
			{
				continue;
			}
			else if (limitType == "include")
			{
				limitTypeIsInclude = true;
			}
			else if (limitType == "exclude")
			{
				limitTypeIsInclude = false;
			}
			else
			{
				try
				{
					fatal2(__("the option '%s' can have only values 'none', 'include' or 'exclude'"),
							limitTypeOptionName);
				}
				catch (Exception&)
				{
					continue;
				}
			}
			auto limitValues = config.getList(limitValuesOptionName);

			__items.push_back(Item { categoryIt->second, limitTypeIsInclude, limitValues });
		}
	}

	bool isExcluded(const ReleaseInfo& releaseInfo) const
	{
		FORIT(itemIt, __items)
		{
			bool foundInLimitValues = std::find(itemIt->values.begin(), itemIt->values.end(),
					(itemIt->attributeExtractor)(releaseInfo)) != itemIt->values.end();
			if (itemIt->typeIsInclude != foundInLimitValues)
			{
				return true;
			}
		}

		return false;
	}
};

void CacheImpl::processIndexEntries(bool useBinary, bool useSource)
{
	ReleaseLimits releaseLimits(*config);
	for (const auto& entry: indexEntries)
	{
		if (entry.category == IndexEntry::Binary && !useBinary)
		{
			continue;
		}
		if (entry.category == IndexEntry::Source && !useSource)
		{
			continue;
		}

		processIndexEntry(entry, releaseLimits);
	}
}

static string getIndexEntryOptionValue(const Cache::IndexEntry& entry, const string& key)
{
	auto it = entry.options.find(key);
	if (it == entry.options.end())
	{
		return string();
	}
	else
	{
		return it->second;
	}
}

static bool getVerifiedBitForIndexEntry(const Cache::IndexEntry& entry,
		const Config& config, const string& path, const string& alias)
{
	auto trustedOptionValue = getIndexEntryOptionValue(entry, "trusted");
	if (trustedOptionValue == "yes")
	{
		return true;
	}
	else if (trustedOptionValue == "no")
	{
		return false;
	}
	else
	{
		return cachefiles::verifySignature(config, path, alias);
	}
}

shared_ptr< ReleaseInfo > CacheImpl::getReleaseInfo(const Config& config, const IndexEntry& indexEntry)
{
	auto path = cachefiles::getPathOfMasterReleaseLikeList(config, indexEntry);
	auto insertResult = releaseInfoCache.insert({ path, {} });
	auto& cachedValue = insertResult.first->second;
	if (insertResult.second)
	{
		auto alias = indexEntry.uri + ' ' + indexEntry.distribution;
		if (path.empty())
		{
			warn2(__("no release file present for '%s'"), alias);
		}
		else
		{
			cachedValue = cachefiles::getReleaseInfo(config, path, alias);
			cachedValue->verified = getVerifiedBitForIndexEntry(indexEntry, config, path, alias);
		}
	}
	if (!cachedValue)
	{
		throw Exception(""); // !cachedValue means that getReleaseInfo has failed before
	}
	return shared_ptr< ReleaseInfo > (new ReleaseInfo(*cachedValue));
}

void CacheImpl::processIndexEntry(const IndexEntry& indexEntry,
		const ReleaseLimits& releaseLimits)
{
	string indexFileToParse = cachefiles::getPathOfIndexList(*config, indexEntry);

	string indexAlias = indexEntry.uri + ' ' + indexEntry.distribution + ' ' +
			indexEntry.component + ' ' +
			((indexEntry.category == IndexEntry::Binary) ? "(binary)" : "(source)");

	shared_ptr< ReleaseInfo > releaseInfo;
	try
	{
		releaseInfo = getReleaseInfo(*config, indexEntry);
		releaseInfo->component = indexEntry.component;
		releaseInfo->baseUri = indexEntry.uri;

		if (releaseLimits.isExcluded(*releaseInfo))
		{
			return;
		}

		if (indexEntry.category == IndexEntry::Binary)
		{
			binaryReleaseData.push_back(releaseInfo);
		}
		else
		{
			sourceReleaseData.push_back(releaseInfo);
		}

		processIndexFile(indexFileToParse, indexEntry.category, releaseInfo, indexAlias);
	}
	catch (Exception&)
	{
		warn2(__("skipped the index '%s'"), indexAlias);
	}

	if (releaseInfo && Version::parseInfoOnly) // description is info-only field
	{
		processTranslationFiles(indexEntry, indexAlias);
	}
}

void CacheImpl::processTranslationFiles(const IndexEntry& indexEntry,
		const string& indexAlias)
{
	auto process = [this](const string& path, const string& localizationAlias)
	{
		try
		{
			if (fs::fileExists(path))
			{
				processTranslationFile(path, localizationAlias);
			}
		}
		catch (Exception&)
		{
			warn2(__("skipped the index '%s'"), localizationAlias);
		}
	};

	auto localizationRecords = cachefiles::getPathsOfLocalizedDescriptions(*config, indexEntry);
	for (const auto& record: localizationRecords)
	{
		auto description = format2(__("'%s' descriptions localization"), record.first);
		auto localizationAlias = format2(__("%s for '%s'"), description, indexAlias);
		process(record.second, localizationAlias);
	}
}

void CacheImpl::processIndexFile(const string& path, IndexEntry::Type category,
		shared_ptr< const ReleaseInfo > releaseInfo, const string& alias)
{
	auto& prePackagesStorage = (category == IndexEntry::Binary ?
			preBinaryPackages : preSourcePackages);

	shared_ptr< File > file(new RequiredFile(path, "r"));

	releaseInfoAndFileStorage.push_back(make_pair(releaseInfo, file));
	PrePackageRecord prePackageRecord;
	prePackageRecord.releaseInfoAndFile = &*(releaseInfoAndFileStorage.rbegin());

	try
	{
		string packageName;
		const string* persistentPackageNamePtr;

		ioi::Record ioiRecord;
		ioiRecord.offsetPtr = &prePackageRecord.offset;
		ioiRecord.indexStringPtr = &packageName;

		ioi::ps::Callbacks callbacks;
		callbacks.main =
				[this, &packageName, &alias, &prePackagesStorage, &prePackageRecord, &persistentPackageNamePtr]()
				{
					try
					{
						checkPackageName(packageName);
					}
					catch (Exception&)
					{
						warn2(__("discarding this package version from the index '%s'"), alias);
						return;
					}

					auto& prePackageRecords = prePackagesStorage[std::move(packageName)];
					prePackageRecords.push_back(prePackageRecord);

					persistentPackageNamePtr = (const string*)
							((const char*)(&prePackageRecords) - offsetof(PrePackageMap::value_type, second));
				};
		callbacks.provides =
				[this, &persistentPackageNamePtr](const char* begin, const char* end)
				{
					processProvides(persistentPackageNamePtr, begin, end);
				};

		ioi::ps::processIndex(path, callbacks, ioiRecord);
	}
	catch (Exception&)
	{
		fatal2(__("unable to parse the index '%s'"), alias);
	}
}

void CacheImpl::processTranslationFile(const string& path, const string& alias)
{
	translationFileStorage.emplace_back(path, "r");

	File* file = &translationFileStorage.back();
	try
	{
		string md5;
		TranslationPosition translationPosition;
		translationPosition.file = file;

		ioi::Record ioiRecord = { &translationPosition.offset, &md5 };

		ioi::tr::Callbacks callbacks;
		callbacks.main =
				[this, &md5, &translationPosition]()
				{
					translations.insert({ std::move(md5), translationPosition });
				};

		ioi::tr::processIndex(path, callbacks, ioiRecord);
	}
	catch(Exception&)
	{
		fatal2(__("unable to parse the index '%s'"), alias);
	}
}

void CacheImpl::parsePreferences()
{
	pinInfo.reset(new PinInfo(config, systemState.get()));
}

ssize_t CacheImpl::computePin(const Version* version, const BinaryPackage* binaryPackage) const
{
	auto getInstalledVersionString = [&binaryPackage]() -> const string&
	{
		static const string emptyString;
		if (binaryPackage)
		{
			auto installedVersion = binaryPackage->getInstalledVersion();
			if (installedVersion)
			{
				return installedVersion->versionString;
			}
		}
		return emptyString;
	};

	const auto& installedVersionString = getInstalledVersionString();
	auto result = pinInfo->getPin(version, installedVersionString);

	if (version->versionString == installedVersionString)
	{
		for (const auto& otherVersion: *binaryPackage)
		{
			if (otherVersion == version) continue;
			if (getOriginalVersionString(otherVersion->versionString).equal(
					getOriginalVersionString(installedVersionString)))
			{
				auto otherPin = getPin(otherVersion, [&binaryPackage]() { return binaryPackage; });
				if (otherPin > result) result = otherPin;
			}
		}
	}
	return result;
}

ssize_t CacheImpl::getPin(const Version* version,
		const std::function< const BinaryPackage* () >& getBinaryPackage) const
{
	if (Cache::memoize)
	{
		auto it = pinCache.find(version);
		if (it != pinCache.end())
		{
			return it->second;
		}
	}

	auto result = computePin(version, getBinaryPackage());
	if (Cache::memoize)
	{
		pinCache.insert({ version, result });
	}
	return result;
}

string CacheImpl::getLocalizedDescription(const BinaryVersion* version) const
{
	const string& sourceHash = !version->descriptionHash.empty() ?
			version->descriptionHash :
			HashSums::getHashOfString(HashSums::MD5, version->description);

	auto it = translations.find(sourceHash);
	if (it != translations.end())
	{
		const TranslationPosition& position = it->second;
		position.file->seek(position.offset);
		return position.file->getRecord().chompAsRecord();
	}
	return version->description;
}

void CacheImpl::parseExtendedStates()
{
	// we are mainly parsing duals like:

	// Package: perl
	// Auto-Installed: 1

	// but another fields may be present

	try
	{
		string path = cachefiles::getPathOfExtendedStates(*config);

		string openError;
		File file(path, "r", openError);
		if (!openError.empty())
		{
			warn2(__("unable to open the extended states file '%s': %s"), path, openError);
			return;
		}

		p_parseExtendedStatesContent(file);
	}
	catch (Exception&)
	{
		fatal2(__("unable to parse extended states"));
	}
}

void CacheImpl::p_parseExtendedStatesContent(File& contentFile)
{
	internal::TagParser parser(&contentFile);
	internal::TagParser::StringRange tagName, tagValue;

	while (parser.parseNextLine(tagName, tagValue) && !contentFile.eof())
	{
		if (!tagName.equal("Package", 7))
		{
			fatal2(__("wrong tag: expected 'Package', got '%s'"), tagName.toString());
		}

		string packageName = tagValue.toString();
		auto& rawRecord = extendedInfo.raw[packageName];

		while (parser.parseNextLine(tagName, tagValue))
		{
			rawRecord[tagName.toString()] = tagValue.toString();

			if (tagName.equal(BUFFER_AND_SIZE("Auto-Installed")))
			{
				if (tagValue.equal(BUFFER_AND_SIZE("1")))
				{
					// adding to storage
					extendedInfo.automaticallyInstalled.insert(packageName);
				}
				else if (!tagValue.equal(BUFFER_AND_SIZE("0")))
				{
					fatal2(__("bad value '%s' (should be 0 or 1) for the package '%s'"),
							tagValue.toString(), packageName);
				}
			}
		}
	}
}

vector< const BinaryVersion* >
CacheImpl::getSatisfyingVersionsNonCached(const RelationExpression& relationExpression) const
{
	auto result = getSatisfyingVersionsNonCached(relationExpression[0]);

	// now, if alternatives (OR groups) are present, we should add them too,
	// without duplicates, but without sorting to not change the order, specified
	// in relation expression
	for (auto relationIt = relationExpression.begin() + 1;
		relationIt != relationExpression.end(); ++relationIt)
	{
		auto source = getSatisfyingVersionsNonCached(*relationIt);
		for (const auto& version: source)
		{
			if (std::find(result.begin(), result.end(), version) == result.end())
			{
				result.push_back(version);
			}
		}
	}

	return result;
}

vector< const BinaryVersion* >
CacheImpl::getSatisfyingVersions(const RelationExpression& relationExpression) const
{
	if (Cache::memoize)
	{
		// caching results
		auto key = relationExpression.getHashString();
		auto it = getSatisfyingVersionsCache.find(key);
		if (it != getSatisfyingVersionsCache.end())
		{
			return it->second;
		}
		else
		{
			auto& result = getSatisfyingVersionsCache[std::move(key)];
			result = getSatisfyingVersionsNonCached(relationExpression);
			return result;
		}
	}
	else
	{
		return getSatisfyingVersionsNonCached(relationExpression);
	}
}

}
}

