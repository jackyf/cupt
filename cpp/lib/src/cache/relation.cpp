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

namespace {

const char* parseWhitespace(const char* start, const char* end)
{
	auto current = start;
	while (current != end && *current == ' ')
	{
		++current;
	}
	return current;
}

template <typename CallbackT>
const char* parseEnclosedWordList(const char* start, const char* end,
		char openingChar, char closingChar, const char* failMessage, const CallbackT& callback)
{
	auto current = start;
	if (current == end || *current != openingChar)
	{
		return current; // no architectures
	}
	++current;

	const char* wordStart = nullptr;
	bool foundClosingBracket = false;
	while (current != end && !foundClosingBracket)
	{
		if (*current == ' ' || *current == closingChar)
		{
			if (wordStart)
			{
				callback(wordStart, current);
				wordStart = nullptr;
			}
			foundClosingBracket = (*current == closingChar);
		}
		else
		{
			if (!wordStart)
			{
				wordStart = current;
			}
		}
		++current;
	}

	if (!foundClosingBracket)
	{
		fatal2(failMessage, string(start, end));
	}

	return current;
}

}

const char* Relation::p_parseVersionPart(const char* current, const char* end)
{
	// parse relation
	if (current == end || *current != '(')
	{
		return current; // no version part detected
	}
	++current;

	current = p_parseRelationSymbols(current, end);
	if (!current)
	{
		return nullptr; // wrong symbols
	}

	current = parseWhitespace(current, end);

	auto versionStringEnd = current;
	while (versionStringEnd != end && *versionStringEnd != ')' && *versionStringEnd != ' ')
	{
		++versionStringEnd;
	}
	versionString.assign(current, versionStringEnd);
	checkVersionString(versionString);

	current = parseWhitespace(versionStringEnd, end);

	if (current == end || *current != ')')
	{
		return nullptr; // no closing bracket detected
	}
	++current;

	return current;
}

const char* Relation::p_parseRelationSymbols(const char* current, const char* end)
{
	if (current + 1 > end)
	{
		return nullptr;
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
			return nullptr;
	}
	return current;
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

	return current;
}

const char* Relation::__init(const char* start, const char* end)
{
	const char* current = p_parsePackagePart(start, end);
	if (!current)
	{
		fatal2(__("failed to parse a package name in the relation '%s'"), string(start, end));
	}
	current = parseWhitespace(current, end);
	current = p_parseVersionPart(current, end);
	if (!current)
	{
		fatal2(__("failed to parse a version part in the relation '%s'"), string(start, end));
	}
	return parseWhitespace(current, end);
}

Relation::Relation(pair<const char*, const char*> input)
{
	if (__init(input.first, input.second) != input.second)
	{
		fatal2(__("failed to parse a suffix in the relation '%s'"), string(input.first, input.second));
	};
}

Relation::Relation(pair<const char*, const char*> input, char const* * end)
{
	*end = __init(input.first, input.second);
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

void ArchitecturedRelation::__init(const char* start, const char* suffixStart, const char* end)
{
	auto current = p_parseArchitectures(suffixStart, end);
	const char* prevCurrent;
	do
	{
		prevCurrent = current;
		current = parseWhitespace(current, end);
		current = p_parseProfiles(current, end);
	} while (current != prevCurrent);

	if (current != end)
	{
		fatal2(__("failed to parse a suffix in the relation '%s'"), string(start, end));
	};
}

const char* ArchitecturedRelation::p_parseArchitectures(const char* start, const char* end)
{
	return parseEnclosedWordList(start, end, '[', ']',
			__("unable to parse architecture filters '%s'"),
			[this](const char* a, const char* b){ architectureFilters.emplace_back(a, b); });
}

const char* ArchitecturedRelation::p_parseProfiles(const char* start, const char* end)
{
	bool isNewList = true;
	auto callback = [this, &isNewList](const char* a, const char* b){
		if (isNewList)
		{
			buildProfiles.emplace_back();
			isNewList = false;
		}
		auto& lastList = buildProfiles.back();
		lastList.emplace_back(a, b);
	};

	return parseEnclosedWordList(start, end, '<', '>',
			__("unable to parse build profiles '%s'"), callback);
}

thread_local static const char* parentEnd;

ArchitecturedRelation::ArchitecturedRelation(pair<const char*, const char*> input)
	: Relation(input, &parentEnd)
{
	__init(input.first, parentEnd, input.second);
}

string ArchitecturedRelation::toString() const
{
	string result = Relation::toString();
	if (!architectureFilters.empty())
	{
		result += string(" [") + join(" ", architectureFilters) + ']';
	}
	for (const auto& profileList: buildProfiles)
	{
		result += string(" <") + join(" ", profileList) + '>';
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

