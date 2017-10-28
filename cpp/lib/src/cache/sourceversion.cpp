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
#include <cupt/cache/sourceversion.hpp>

#include <internal/common.hpp>

namespace cupt {
namespace cache {

bool SourceVersion::areHashesEqual(const Version* other) const
{
	auto o = dynamic_cast< const SourceVersion* >(other);
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
	N__("Tarball"), N__("Diff"), N__("Dsc")
};
const string SourceVersion::RelationTypes::strings[] = {
	N__("Build-Depends"), N__("Build-Depends-Indep"), N__("Build-Depends-Arch"),
	N__("Build-Conflicts"), N__("Build-Conflicts-Indep"), N__("Build-Conflicts-Arch"),
};
const char* SourceVersion::RelationTypes::rawStrings[] = {
	"build-depends", "build-depends-indep", "build-depends-arch",
	"build-conflicts", "build-conflicts-indep", "build-conflicts-arch"
};

}
}

