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

#include <cupt/file.hpp>
#include <cupt/cache/sourceversion.hpp>
#include <cupt/cache/releaseinfo.hpp>

#include <internal/tagparser.hpp>
#include <internal/versionparsemacro.hpp>
#include <internal/common.hpp>
#include <internal/regex.hpp>

namespace cupt {
namespace cache {

shared_ptr< SourceVersion > SourceVersion::parseFromFile(const Version::InitializationParameters& initParams)
{
	auto v = std::make_shared< SourceVersion >();

	Source source;

	v->packageName = initParams.packageName;
	source.release = initParams.releaseInfo;

	v->priority = Version::Priorities::Extra; // default value if not specified

	{ // actual parsing
		// go to starting byte of the entry
		initParams.file->seek(initParams.offset);

		internal::TagParser parser(initParams.file.get());
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

				internal::processSpaceCommaSpaceDelimitedStrings(block.begin(), block.end(),
						[&v](string::const_iterator a, string::const_iterator b)
						{
							v->binaryPackageNames.push_back(string(a, b));
						});
			})
			TAG(Directory, source.directory = tagValue;)
			TAG(Version, v->versionString = tagValue;)
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

bool SourceVersion::areHashesEqual(const shared_ptr< const Version >& other) const
{
	shared_ptr< const SourceVersion > o = dynamic_pointer_cast< const SourceVersion >(other);
	if (!o)
	{
		fatal2i("areHashesEqual: non-source version parameter");
	}
	for (size_t i = 0; i < SourceVersion::FileParts::Count; ++i)
	{
		// not perfect algorithm, it requires the same order of files
		auto fileCount = files[i].size();
		auto otherFileCount = o->files[i].size();
		if (fileCount != otherFileCount)
		{
			return false;
		}
		for (size_t j = 0; j < fileCount; ++j)
		{
			if (! files[i][j].hashSums.match(o->files[i][j].hashSums))
			{
				return false;
			}
		}
	}
	return true;
}

const string SourceVersion::FileParts::strings[] = {
	__("Tarball"), __("Diff"), __("Dsc")
};
const string SourceVersion::RelationTypes::strings[] = {
	__("Build-Depends"), __("Build-Depends-Indep"), __("Build-Conflicts"), __("Build-Conflicts-Indep"),
};
const char* SourceVersion::RelationTypes::rawStrings[] = {
	"build-depends", "build-depend-indep", "build-conflicts", "build-conflicts-indep",
};

}
}

