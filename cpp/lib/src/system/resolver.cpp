/**************************************************************************
*   Copyright (C) 2011 by Eugene V. Lyubimkin                             *
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
#include <cupt/system/resolver.hpp>

namespace cupt {
namespace system {

string Resolver::UserReason::toString() const
{
	return __("user request");
}

string Resolver::AutoRemovalReason::toString() const
{
	return __("auto-removal");
}

Resolver::RelationExpressionReason::RelationExpressionReason(
		const BinaryVersion* version_,
		BinaryVersion::RelationTypes::Type dependencyType_,
		const cache::RelationExpression& relationExpression_)
	: version(version_), dependencyType(dependencyType_),
	relationExpression(relationExpression_)
{}

string Resolver::RelationExpressionReason::toString() const
{
	static const map< BinaryVersion::RelationTypes::Type, string > dependencyTypeTranslations = {
		{ BinaryVersion::RelationTypes::PreDepends, __("pre-depends on") },
		{ BinaryVersion::RelationTypes::Depends, __("depends on") },
		{ BinaryVersion::RelationTypes::Recommends, __("recommends") },
		{ BinaryVersion::RelationTypes::Suggests, __("suggests") },
		{ BinaryVersion::RelationTypes::Conflicts, __("conflicts with") },
		{ BinaryVersion::RelationTypes::Breaks, __("breaks") },
	};

	auto dependencyTypeTranslationIt = dependencyTypeTranslations.find(dependencyType);
	if (dependencyTypeTranslationIt == dependencyTypeTranslations.end())
	{
		warn2(__("unsupported reason dependency type '%s'"),
				BinaryVersion::RelationTypes::strings[dependencyType]);
		return string();
	}
	else
	{
		return format2("%s %s %s '%s'", version->packageName, version->versionString,
				dependencyTypeTranslationIt->second, relationExpression.toString());
	}
}

Resolver::SynchronizationReason::SynchronizationReason(
		const BinaryVersion* version_,
		const string& packageName_)
	: version(version_), relatedPackageName(packageName_)
{}

string Resolver::SynchronizationReason::toString() const
{
	return format2(__("%s: synchronization with %s %s"), relatedPackageName,
			version->packageName, version->versionString);
}

}
}
