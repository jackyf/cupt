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
#include <cupt/cache/binaryversion.hpp>
#include <cupt/cache/releaseinfo.hpp>

#include <internal/common.hpp>

namespace cupt {
namespace cache {

bool BinaryVersion::isInstalled() const
{
	return sources.empty() ? false : sources[0].release->baseUri.empty();
}

bool BinaryVersion::areHashesEqual(const Version* other) const
{
	auto o = dynamic_cast< const BinaryVersion* >(other);
	if (!o)
	{
		fatal2i("areHashesEqual: non-binary version parameter"); /// LCOV_EXCL_LINE
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

