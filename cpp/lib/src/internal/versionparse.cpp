/**************************************************************************
*   Copyright (C) 2013 by Eugene V. Lyubimkin                             *
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
#include <cupt/file.hpp>
#include <cupt/cache/binaryversion.hpp>
#include <cupt/cache/sourceversion.hpp>
#include <cupt/cache/releaseinfo.hpp>

#include <internal/common.hpp>
#include <internal/tagparser.hpp>
#include <internal/parse.hpp>
#include <internal/regex.hpp>

namespace cupt {
namespace internal {

using namespace cache;

#define TAG(tagNameParam, code) \
		{ \
			if (tagName.equal(BUFFER_AND_SIZE( #tagNameParam ))) \
			{ \
				code \
				continue; \
			} \
		}

#define PARSE_PRIORITY \
		TAG(Priority, \
		{ \
			if (tagValue.equal(BUFFER_AND_SIZE("required"))) \
			{ \
				v->priority = Version::Priorities::Required; \
			} \
			else if (tagValue.equal(BUFFER_AND_SIZE("important"))) \
			{ \
				v->priority = Version::Priorities::Important; \
			} \
			else if (tagValue.equal(BUFFER_AND_SIZE("standard"))) \
			{ \
				v->priority = Version::Priorities::Standard; \
			} \
			else if (tagValue.equal(BUFFER_AND_SIZE("optional"))) \
			{ \
				v->priority = Version::Priorities::Optional; \
			} \
			else if (tagValue.equal(BUFFER_AND_SIZE("extra"))) \
			{ \
				v->priority = Version::Priorities::Extra; \
			} \
			else \
			{ \
				warn2("package %s, version %s: unrecognized priority value '%s', using 'extra' instead", \
						v->packageName, v->versionString, string(tagValue)); \
			} \
		})

#define PARSE_OTHERS \
			if (Version::parseOthers || (Version::parseInfoOnly && tagName.equal(BUFFER_AND_SIZE("Description-md5")))) \
			{ \
				if (!tagName.equal(BUFFER_AND_SIZE("Package")) && !tagName.equal(BUFFER_AND_SIZE("Status"))) \
				{ \
					if (!v->others) \
					{ \
						v->others = new map< string, string >; \
					} \
					string tagNameString(tagName); \
					(*(v->others))[tagNameString] = tagValue; \
				} \
			}

unique_ptr< BinaryVersion > parseBinaryVersion(const Version::InitializationParameters& initParams)
{
	typedef BinaryVersion::RelationTypes RelationTypes;

	unique_ptr< BinaryVersion > v(new BinaryVersion);

	Version::Source source;

	v->essential = false;
	v->packageName = *initParams.packageNamePtr;
	source.release = initParams.releaseInfo;

	v->installedSize = 0;
	v->priority = Version::Priorities::Extra; // default value if not specified
	v->file.size = 0;

	{ // actual parsing
		// go to starting byte of the entry
		initParams.file->seek(initParams.offset);

		internal::TagParser parser(initParams.file);
		internal::TagParser::StringRange tagName, tagValue;

		while (parser.parseNextLine(tagName, tagValue))
		{
			TAG(Version, v->versionString = tagValue;)
			TAG(Essential, v->essential = (string(tagValue) == "yes");)
			PARSE_PRIORITY
			TAG(Size, v->file.size = internal::string2uint32(tagValue);)
			TAG(Installed-Size, v->installedSize = internal::string2uint32(tagValue) * 1024;)
			TAG(Architecture, v->architecture = tagValue;)
			TAG(Filename,
			{
				string filename = tagValue;
				auto lastSlashPosition = filename.find_last_of('/');
				if (lastSlashPosition == string::npos)
				{
					// source.directory remains empty
					v->file.name = filename;
				}
				else
				{
					source.directory = filename.substr(0, lastSlashPosition);
					v->file.name = filename.substr(lastSlashPosition + 1);
				}
			})
			TAG(MD5sum, v->file.hashSums[HashSums::MD5] = tagValue;)
			TAG(SHA1, v->file.hashSums[HashSums::SHA1] = tagValue;)
			TAG(SHA256, v->file.hashSums[HashSums::SHA256] = tagValue;)
			TAG(Source,
			{
				v->sourcePackageName = tagValue;
				string& value = v->sourcePackageName;
				// determing do we have source version appended or not?
				// example: "abcd (1.2-5)"
				auto size = value.size();
				if (size > 2 && value[size-1] == ')')
				{
					auto delimiterPosition = value.rfind('(', size-2);
					if (delimiterPosition != string::npos)
					{
						// found! there is a source version, most probably
						// indicating that it was some binary-only rebuild, and
						// the source version is different with binary one
						v->sourceVersionString = value.substr(delimiterPosition+1, size-delimiterPosition-2);
						checkVersionString(v->sourceVersionString);
						if (delimiterPosition != 0 && value[delimiterPosition-1] == ' ')
						{
							--delimiterPosition;
						}
						v->sourcePackageName.erase(delimiterPosition);
					}
				}
			};)

			if (Version::parseRelations)
			{
				TAG(Pre-Depends, v->relations[RelationTypes::PreDepends] = RelationLine(tagValue);)
				TAG(Depends, v->relations[RelationTypes::Depends] = RelationLine(tagValue);)
				TAG(Recommends, v->relations[RelationTypes::Recommends] = RelationLine(tagValue);)
				TAG(Suggests, v->relations[RelationTypes::Suggests] = RelationLine(tagValue);)
				TAG(Conflicts, v->relations[RelationTypes::Conflicts] = RelationLine(tagValue);)
				TAG(Breaks, v->relations[RelationTypes::Breaks] = RelationLine(tagValue);)
				TAG(Replaces, v->relations[RelationTypes::Replaces] = RelationLine(tagValue);)
				TAG(Enhances, v->relations[RelationTypes::Enhances] = RelationLine(tagValue);)
				TAG(Provides,
				{
					auto callback = [&v](string::const_iterator begin, string::const_iterator end)
					{
						v->provides.push_back(string(begin, end));
					};
					internal::parse::processSpaceCharSpaceDelimitedStrings(
							tagValue.first, tagValue.second, ',', callback);
				})
			}

			if (Version::parseInfoOnly)
			{
				TAG(Section, v->section = tagValue;)
				TAG(Maintainer, v->maintainer = tagValue;)
				TAG(Description,
				{
					v->description = tagValue;
					v->description.append("\n");
					parser.parseAdditionalLines(v->description);
				};)
				TAG(Tag, v->tags = tagValue;)
				PARSE_OTHERS
			}
		}

		checkVersionString(v->versionString);
		if (v->sourceVersionString.empty())
		{
			v->sourceVersionString = v->versionString;
		}
		if (v->sourcePackageName.empty())
		{
			v->sourcePackageName = v->packageName;
		}
	};

	if (v->versionString.empty())
	{
		fatal2(__("version string isn't defined"));
	}
	if (v->architecture.empty())
	{
		warn2(__("binary package %s, version %s: architecture isn't defined, setting it to 'all'"),
				v->packageName, v->versionString);
		v->architecture = "all";
	}
	v->sources.push_back(source);
	if (!v->isInstalled() && v->file.hashSums.empty())
	{
		fatal2(__("no hash sums specified"));
	}

	return v;
}

unique_ptr< SourceVersion > parseSourceVersion(const Version::InitializationParameters& initParams)
{
	typedef SourceVersion::RelationTypes RelationTypes;

	unique_ptr< SourceVersion > v(new SourceVersion);

	Version::Source source;

	v->packageName = *initParams.packageNamePtr;
	source.release = initParams.releaseInfo;

	v->priority = Version::Priorities::Extra; // default value if not specified

	{ // actual parsing
		// go to starting byte of the entry
		initParams.file->seek(initParams.offset);

		internal::TagParser parser(initParams.file);
		internal::TagParser::StringRange tagName, tagValue;

		static const sregex checksumsLineRegex = sregex::compile(" ([[:xdigit:]]+) +(\\d+) +(.*)", regex_constants::optimize);
		static const sregex dscPartRegex = sregex::compile("\\.dsc$", regex_constants::optimize);
		static const sregex diffPartRegex = sregex::compile("\\.(?:diff\\.gz|debian\\.tar\\.\\w+)$", regex_constants::optimize);
		smatch lineMatch;
		smatch m;

		auto parseChecksumRecord = [&](HashSums::Type hashSumType)
		{
			if (tagValue.first != tagValue.second)
			{
				fatal2(__("unexpected non-empty tag value '%s'"), string(tagValue));
			}
			string block;
			parser.parseAdditionalLines(block);
			auto lines = internal::split('\n', block);
			for (const string& line: lines)
			{
				if (!regex_match(line, lineMatch, checksumsLineRegex))
				{
					fatal2(__("malformed line '%s'"), line);
				}
				const string name = lineMatch[3];

				SourceVersion::FileParts::Type part = (regex_search(name, m, dscPartRegex) ? SourceVersion::FileParts::Dsc :
						(regex_search(name, m, diffPartRegex) ? SourceVersion::FileParts::Diff : SourceVersion::FileParts::Tarball));
				bool foundRecord = false;
				FORIT(recordIt, v->files[part])
				{
					if (recordIt->name == name)
					{
						recordIt->hashSums[hashSumType] = lineMatch[1];
						foundRecord = true;
						break;
					}
				}

				if (!foundRecord)
				{
					SourceVersion::FileRecord& fileRecord =
							(v->files[part].push_back(SourceVersion::FileRecord()), *(v->files[part].rbegin()));
					fileRecord.name = name;
					fileRecord.size = internal::string2uint32(lineMatch[2]);
					fileRecord.hashSums[hashSumType] = lineMatch[1];
				}
			}
		};

		while (parser.parseNextLine(tagName, tagValue))
		{
			// parsing checksums and file names
			TAG(Files, parseChecksumRecord(HashSums::MD5);)
			TAG(Checksums-Sha1, parseChecksumRecord(HashSums::SHA1);)
			TAG(Checksums-Sha256, parseChecksumRecord(HashSums::SHA256);)

			TAG(Binary,
			{
				auto block = string(tagValue);
				string additionalLines;
				parser.parseAdditionalLines(additionalLines);
				if (!additionalLines.empty())
				{
					auto lastCharacterIt = additionalLines.end() - 1;
					if (*lastCharacterIt == '\n')
					{
						additionalLines.erase(lastCharacterIt);
					}
					FORIT(charIt, additionalLines)
					{
						if (*charIt == '\n')
						{
							*charIt = ' ';
						}
					}
					block.append(additionalLines);
				}

				internal::parse::processSpaceCharSpaceDelimitedStrings(
						block.begin(), block.end(), ',',
						[&v](string::const_iterator a, string::const_iterator b)
						{
							v->binaryPackageNames.push_back(string(a, b));
						});
			})
			TAG(Directory, source.directory = tagValue;)
			TAG(Version, v->versionString = tagValue;)
			if (tagName.equal(BUFFER_AND_SIZE("Priority")) && tagValue.equal(BUFFER_AND_SIZE("source")))
			{
				continue; // a workaround for the unannounced value 'source' (Debian BTS #626394)
			}
			PARSE_PRIORITY
			TAG(Architecture, v->architectures = internal::split(' ', tagValue);)

			if (Version::parseRelations)
			{
				TAG(Build-Depends, v->relations[RelationTypes::BuildDepends] = ArchitecturedRelationLine(tagValue);)
				TAG(Build-Depends-Indep, v->relations[RelationTypes::BuildDependsIndep] = ArchitecturedRelationLine(tagValue);)
				TAG(Build-Conflicts, v->relations[RelationTypes::BuildConflicts] = ArchitecturedRelationLine(tagValue);)
				TAG(Build-Conflicts-Indep, v->relations[RelationTypes::BuildConflictsIndep] = ArchitecturedRelationLine(tagValue);)
			}

			if (Version::parseInfoOnly)
			{
				TAG(Section, v->section = tagValue;)
				TAG(Maintainer, v->maintainer = tagValue;)
				static const sregex commaSeparatedRegex = sregex::compile("\\s*,\\s*", regex_constants::optimize);
				TAG(Uploaders, v->uploaders = split(commaSeparatedRegex, tagValue);)
				PARSE_OTHERS
			}
		}
	}
	checkVersionString(v->versionString);
	v->sources.push_back(source);

	if (v->versionString.empty())
	{
		fatal2(__("version string isn't defined"));
	}
	if (v->architectures.empty())
	{
		warn2(__("source package %s, version %s: architectures aren't defined, setting them to 'all'"),
				v->packageName, v->versionString);
		v->architectures.push_back("all");
	}
	// no need to verify hash sums for emptyness, it's guarantted by parsing algorithm above

	return v;
}

}
}

