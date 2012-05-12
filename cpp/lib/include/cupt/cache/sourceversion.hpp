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
#ifndef CUPT_CACHE_SOURCEVERSION_SEEN
#define CUPT_CACHE_SOURCEVERSION_SEEN

/// @file

#include <cupt/hashsums.hpp>
#include <cupt/cache/version.hpp>
#include <cupt/cache/relation.hpp>

namespace cupt {
namespace cache {

/// source version info
struct CUPT_API SourceVersion: public Version
{
	/// build-time relation types between source version and binary versions
	struct RelationTypes
	{
		/// type
		enum Type { BuildDepends, BuildDependsIndep, BuildConflicts, BuildConflictsIndep, Count };
		static const string strings[]; ///< @copydoc BinaryVersion::RelationTypes::strings
		static const char* rawStrings[]; ///< @copydoc BinaryVersion::RelationTypes::rawStrings
	};
	/// file parts
	/**
	 * Each source version may contain several files as physical parts.
	 */
	struct FileParts
	{
		/// type
		enum Type { Tarball, Diff, Dsc, Count };
		static const string strings[]; ///< string values of corresponding types
	};
	ArchitecturedRelationLine relations[RelationTypes::Count]; ///< relations
	vector< FileRecord > files[FileParts::Count]; ///< Version::FileRecord s
	vector< string > uploaders; ///< array of uploaders
	vector< string > binaryPackageNames; ///< array of binary package names, which are built out of
	vector< string > architectures; ///< array of binary architectures on which this source version may be built

	virtual bool areHashesEqual(const Version* other) const;

	/// parse version
	static SourceVersion* parseFromFile(const Version::InitializationParameters&);
};

} // namespace
} // namespace

#endif

