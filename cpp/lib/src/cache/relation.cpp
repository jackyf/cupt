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
#include <algorithm>

#include <cupt/common.hpp>
#include <cupt/cache/relation.hpp>
#include <cupt/packagename.hpp>
#include <cupt/versionstring.hpp>

#include <internal/common.hpp>
#include <internal/parse.hpp>

namespace cupt {
namespace cache {

bool Relation::__parse_versioned_info(const char* current, const char* end)
{
	// parse relation
	if (current == end || current+1 == end /* version should at least have one character */)
	{
		return false;
	}
	switch (*current)
	{
		case '>':
		{
			if (*(current+1) == '=')
			{
				relationType = Types::MoreOrEqual;
				current += 2;
			}
			else if (*(current+1) == '>')
			{
				relationType = Types::More;
				current += 2;
			}
			else
			{
				relationType = Types::More;
				current += 1;
			}
		}
		break;
		case '=':
		{
			relationType = Types::Equal;
			current += 1;
		}
		break;
		case '<':
		{
			if (*(current+1) == '=')
			{
				relationType = Types::LessOrEqual;
				current += 2;
			}
			else if (*(current+1) == '<')
			{
				relationType = Types::Less;
				current += 2;
			}
			else
			{
				relationType = Types::Less;
				current += 1;
			}
		}
		break;
		default:
			return false;
	}
	while (current != end && *current == ' ')
	{
		++current;
	}
	const char* versionStringEnd = current+1;
	while (versionStringEnd != end && *versionStringEnd != ')' && *versionStringEnd != ' ')
	{
		++versionStringEnd;
	}
	if (versionStringEnd == end)
	{
		return false; // at least ')' after version string should be
	}
	versionString.assign(current, versionStringEnd);
	checkVersionString(versionString);

	current = versionStringEnd;
	while (current != end && *current == ' ')
	{
		++current;
	}
	if (current == end || *current != ')')
	{
		return false;
	}
	++current;
	while (current != end && *current == ' ')
	{
		++current;
	}
	return (current == end);
}

const char* Relation::p_parsePackagePart(const char* start, const char* end)
{
	const char* current;

	consumePackageName(start, end, current);
	if (current == start) return nullptr;

	packageName.assign(start, current);
	relationType = Types::None;

	if (current != end && *current == ':')
	{
		start = current+1;
		consumePackageName(start, end, current);
		if (current == start) return nullptr;

		architecture.assign(start, current);
	}

	while (current != end && *current != '(')
	{
		if (*current != ' ')
		{
			return nullptr;
		}
		++current;
	}
	return current;
}

void Relation::__init(const char* start, const char* end)
{
	const char* current = p_parsePackagePart(start, end);
	if (!current)
	{
		// bad character in the middle of package name
		fatal2(__("failed to parse a package name in the relation '%s'"), string(start, end));
	}
	else if (current != end)
	{
		++current;
		// okay, here we should have a versoined info
		if (!__parse_versioned_info(current, end))
		{
			string unparsed(start, end);
			fatal2(__("failed to parse a version part in the relation '%s'"), unparsed);
		}
	}
}

Relation::Relation(pair< const char*, const char* > input)
{
	__init(input.first, input.second);
}

Relation::~Relation()
{}

string Relation::toString() const
{
	string result = packageName;
	if (!architecture.empty())
	{
		result += ':';
		result += architecture;
	}
	if (relationType != Types::None)
	{
		// there is versioned info
		result += string(" (") + Types::strings[relationType] + ' ' + versionString + ")";
	}
	return result;
}

bool Relation::isSatisfiedBy(const string& otherVersionString) const
{
	if (relationType == Types::None)
	{
		return true;
	}
	else if (relationType == Types::LiteralyEqual)
	{
		return versionString == otherVersionString;
	}
	else
	{
		// relation is defined, checking
		auto comparisonResult = compareVersionStrings(otherVersionString, versionString);
		switch (relationType)
		{
			case Types::MoreOrEqual:
				return (comparisonResult >= 0);
			case Types::Less:
				return (comparisonResult < 0);
			case Types::LessOrEqual:
				return (comparisonResult <= 0);
			case Types::Equal:
				return (comparisonResult == 0);
			case Types::More:
				return (comparisonResult > 0);
			default:
				__builtin_unreachable();
		}
	}
	__builtin_unreachable();
}

bool Relation::operator==(const Relation& other) const
{
	return (packageName == other.packageName &&
			relationType == other.relationType &&
			versionString == other.versionString);
}

const string Relation::Types::strings[] = { "<<", "=", ">>", "<=", ">=", "===" };

void ArchitecturedRelation::__init(const char* start, const char* end)
{
	if (start == end)
	{
		return; // no architecture filters
	}
	if (*start != '[' || *(end-1) != ']')
	{
		fatal2(__("unable to parse architecture filters '%s'"), string(start, end));
	}
	++start;
	--end;

	architectureFilters = internal::split(' ', string(start, end));
}

ArchitecturedRelation::ArchitecturedRelation(
		pair< const char*, const char* > input)
	: Relation(std::make_pair(input.first, std::find(input.first, input.second, '[')))
{
	__init(std::find(input.first, input.second, '['), input.second);
}

string ArchitecturedRelation::toString() const
{
	static const string openingBracket = "[";
	static const string closingBracket = "]";
	static const string space = " ";
	string result = Relation::toString();
	if (!architectureFilters.empty())
	{
		result += space + openingBracket + join(" ", architectureFilters) + closingBracket;
	}
	return result;
}

bool __is_architectured_relation_eligible(
		const ArchitecturedRelation& architecturedRelation, const string& currentArchitecture)
{
	const vector< string >& architectureFilters = architecturedRelation.architectureFilters;

	if (architectureFilters.empty())
	{
		return true;
	}

	if (!architectureFilters[0].empty() && architectureFilters[0][0] == '!')
	{
		// negative architecture specifications, see Debian Policy §7.1
		for (string architectureFilter: architectureFilters)
		{
			if (architectureFilter.empty() || architectureFilter[0] != '!')
			{
				warn2(__("non-negative architecture filter '%s'"), architectureFilter);
			}
			else
			{
				architectureFilter = architectureFilter.substr(1);
			}
			if (internal::architectureMatch(currentArchitecture, architectureFilter))
			{
				return false; // not our case
			}
		}
		return true;
	}
	else
	{
		// positive architecture specifications, see Debian Policy §7.1
		for (const string& architectureFilter: architectureFilters)
		{
			if (internal::architectureMatch(currentArchitecture, architectureFilter))
			{
				return true; // our case
			}
		}
		return false;
	}
}

RelationLine ArchitecturedRelationLine::toRelationLine(const string& currentArchitecture) const
{
	RelationLine result;

	for (const auto& architecturedRelationExpression: *this)
	{
		RelationExpression newRelationExpression;

		for (const auto& architecturedRelation: architecturedRelationExpression)
		{
			if (__is_architectured_relation_eligible(architecturedRelation, currentArchitecture))
			{
				newRelationExpression.push_back(Relation(architecturedRelation));
			}
		}

		if (!newRelationExpression.empty())
		{
			result.push_back(std::move(newRelationExpression));
		}
	}

	return result;
}

string RelationExpression::getHashString() const
{
	size_t targetLength = 0;
	for (const Relation& relation: *this)
	{
		targetLength += 1 + relation.packageName.size();
		if (relation.relationType != Relation::Types::None)
		{
			targetLength += relation.versionString.size() + 1;
		}
	}
	if (targetLength) // not empty relation expression
	{
		targetLength -= 1;
	}

	string result(targetLength, '\0');
	auto p = result.begin();
	auto beginIt = p;

	for (const Relation& relation: *this)
	{
		if (p != beginIt) // not a start
		{
			*(p++) = '|';
		}

		p = std::copy(relation.packageName.begin(), relation.packageName.end(), p);

		if (relation.relationType != Relation::Types::None)
		{
			// this assertion assures that '\1' + relation.relationType is a
			// non-printable character which cannot be a part of packageName
			static_assert(Relation::Types::None < 16, "internal error: Relation::Types::None >= 16'");
			*(p++) = ('\1' + relation.relationType);

			p = std::copy(relation.versionString.begin(), relation.versionString.end(), p);
		}
	}
	return result;
}


// yes, I know about templates, but here they cause just too much trouble
#define DEFINE_RELATION_EXPRESSION_CLASS(RelationExpressionType, UnderlyingElement) \
void RelationExpressionType::__init(const char* begin, const char* end) \
{ \
	/* split OR groups */ \
	auto callback = [this](const char* begin, const char* end) \
	{ \
		this->emplace_back(std::make_pair(begin, end)); \
	}; \
	internal::parse::processSpaceCharSpaceDelimitedStrings( \
			begin, end, '|', callback); \
} \
 \
RelationExpressionType::RelationExpressionType() \
{} \
 \
RelationExpressionType::RelationExpressionType(pair< const char*, const char* > input) \
{ \
	__init(input.first, input.second); \
} \
 \
RelationExpressionType::RelationExpressionType(const string& expression) \
{ \
	__init(expression.data(), expression.data()+expression.size()); \
} \
\
RelationExpressionType::~RelationExpressionType() \
{} \
 \
string RelationExpressionType::toString() const \
{ \
	vector< string > parts; \
	for (auto it = this->begin(); it != this->end(); ++it) \
	{ \
		parts.push_back(it->toString()); \
	} \
	return join(" | ", parts); \
}

DEFINE_RELATION_EXPRESSION_CLASS(RelationExpression, Relation)
DEFINE_RELATION_EXPRESSION_CLASS(ArchitecturedRelationExpression, ArchitecturedRelation)
#undef DEFINE_RELATION_EXPRESSION_CLASS

#define DEFINE_RELATION_LINE_CLASS(RelationLineType, UnderlyingElement) \
RelationLineType::RelationLineType() \
{} \
 \
void RelationLineType::__init(const char* begin, const char* end) \
{ \
	auto callback = [this](const char* begin, const char* end) \
	{ \
		this->emplace_back(std::make_pair(begin, end)); \
	}; \
 \
	internal::parse::processSpaceCharSpaceDelimitedStrings(begin, end, ',', callback); \
} \
 \
RelationLineType::RelationLineType(pair< const char*, const char* > input) \
{ \
	__init(input.first, input.second); \
} \
 \
RelationLineType::RelationLineType(const string& line) \
{ \
	__init(line.data(), line.data()+line.size()); \
} \
 \
RelationLineType& RelationLineType::operator=(RelationLineType&& other) \
{ \
	std::vector< UnderlyingElement >::swap(other); \
	return *this; \
} \
 \
RelationLineType::~RelationLineType() \
{} \
 \
string RelationLineType::toString() const \
{ \
	vector< string > parts; \
	for (auto it = this->begin(); it != this->end(); ++it) \
	{ \
		parts.push_back(it->toString()); \
	} \
	return join(", ", parts); \
}

DEFINE_RELATION_LINE_CLASS(RelationLine, RelationExpression)
DEFINE_RELATION_LINE_CLASS(ArchitecturedRelationLine, ArchitecturedRelationExpression)
#undef DEFINE_RELATION_LINE_CLASS

}
}

