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
#ifndef CUPT_CACHE_BINARYVERSION_SEEN
#define CUPT_CACHE_BINARYVERSION_SEEN

/// @file

#include <cupt/hashsums.hpp>
#include <cupt/cache/version.hpp>
#include <cupt/cache/relation.hpp>

namespace cupt {
namespace cache {

/// binary version info
struct CUPT_API BinaryVersion: public Version
{
	/// relation types between binary versions
	struct RelationTypes
	{
		/// type
		enum Type { PreDepends, Depends, Recommends, Suggests, Enhances, Conflicts, Breaks, Replaces, Count };
		static const string strings[]; ///< string values of corresponding types
		static const char* rawStrings[]; ///< lower-case, unlocalized string values of corresponding types
	};
	string architecture; ///< binary architecture
	uint32_t installedSize; ///< approximate size of unpacked file content in bytes
	string sourcePackageName; ///< source package name
	string sourceVersionString; ///< source version string
	bool essential; ///< has version 'essential' flag?
	RelationLine relations[RelationTypes::Count]; ///< relations with other binary versions
	vector< string > provides; ///< array of virtual package names
	string description;
	string tags; ///< tags
	FileRecord file; ///< Version::FileRecord

	bool isInstalled() const; ///< is version installed?
	virtual bool areHashesEqual(const Version* other) const;

	/// parse version
	static BinaryVersion* parseFromFile(const Version::InitializationParameters&);
};

} // namespace
} // namespace

#endif

