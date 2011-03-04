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
		const shared_ptr< const BinaryVersion >& version_,
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
		warn("unsupported reason dependency type '%s'",
				BinaryVersion::RelationTypes::strings[dependencyType].c_str());
		return string();
	}
	else
	{
		return sf("%s %s %s '%s'",
				version->packageName.c_str(), version->versionString.c_str(),
				dependencyTypeTranslationIt->second.c_str(),
				relationExpression.toString().c_str());
	}
}

Resolver::SynchronizationReason::SynchronizationReason(const string& packageName_)
	: packageName(packageName_)
{}

string Resolver::SynchronizationReason::toString() const
{
	return sf(__("synchronized with package '%s'"), packageName.c_str());
}

}
}
