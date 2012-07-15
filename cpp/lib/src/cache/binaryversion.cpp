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
#include <cupt/file.hpp>
#include <cupt/cache/binaryversion.hpp>
#include <cupt/cache/releaseinfo.hpp>

#include <internal/tagparser.hpp>
#include <internal/versionparsemacro.hpp>
#include <internal/common.hpp>

namespace cupt {
namespace cache {

BinaryVersion* BinaryVersion::parseFromFile(const Version::InitializationParameters& initParams)
{
	auto v = new BinaryVersion;

	Source source;

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
					internal::processSpaceCommaSpaceDelimitedStrings(tagValue.first, tagValue.second, callback);
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

bool BinaryVersion::isInstalled() const
{
	return sources.empty() ? false : sources[0].release->baseUri.empty();
}

bool BinaryVersion::areHashesEqual(const Version* other) const
{
	auto o = dynamic_cast< const BinaryVersion* >(other);
	if (!o)
	{
		fatal2i("areHashesEqual: non-binary version parameter");
	}
	return file.hashSums.match(o->file.hashSums);
}

const string BinaryVersion::RelationTypes::strings[] = {
	N__("Pre-Depends"), N__("Depends"), N__("Recommends"), N__("Suggests"),
	N__("Enhances"), N__("Conflicts"), N__("Breaks"), N__("Replaces")
};
const char* BinaryVersion::RelationTypes::rawStrings[] = {
	"pre-depends", "depends", "recommends", "suggests",
	"enhances", "conflicts", "breaks", "replaces"
};

}
}

