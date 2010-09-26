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

#include <cupt/common.hpp>
#include <cupt/cache/relation.hpp>

#include <internal/common.hpp>

namespace cupt {
namespace cache {

bool Relation::__parse_versioned_info(string::const_iterator current, string::const_iterator end)
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
	string::const_iterator versionStringEnd = current+1;
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

void Relation::__init(string::const_iterator start, string::const_iterator end)
{
	string::const_iterator current;
	consumePackageName(start, end, current);
	if (current != start)
	{
		// package name is here
		packageName.assign(start, current);
	}
	else
	{
		// no package name, bad
		string unparsed(start, end);
		fatal("failed to parse package name in relation '%s'", unparsed.c_str());
	}

	while (current != end && *current == ' ')
	{
		++current;
	}
	if (current != end && *current == '(')
	{
		++current;
		// okay, here we should have a versoined info
		if (!__parse_versioned_info(current, end))
		{
			string unparsed(start, end);
			fatal("failed to parse versioned info in relation '%s'", unparsed.c_str()); // what else can we do?..
		}
	}
	else
	{
		relationType = Types::None;
	}
}

Relation::Relation(pair< string::const_iterator, string::const_iterator > input)
{
	__init(input.first, input.second);
}

Relation::Relation(const string& unparsed)
{
	__init(unparsed.begin(), unparsed.end());
}

Relation::~Relation()
{}

string Relation::toString() const
{
	string result = packageName;
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
			case Types::None:
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

const string Relation::Types::strings[] = { "<<", "=", ">>", "<=", ">=" };

void ArchitecturedRelation::__init(string::const_iterator start, string::const_iterator end)
{
	if (start == end)
	{
		return; // no architecture filters
	}
	if (*start != '[' || *(end-1) != ']')
	{
		fatal("unable to parse architecture filters '%s'", string(start, end).c_str());
	}
	++start;
	--end;

	architectureFilters = split(' ', string(start, end));
}

ArchitecturedRelation::ArchitecturedRelation(const string& unparsed)
		: Relation(string(unparsed.begin(), std::find(unparsed.begin(), unparsed.end(), '[')))
{
	__init(std::find(unparsed.begin(), unparsed.end(), '['), unparsed.end());
}

ArchitecturedRelation::ArchitecturedRelation(
		pair< string::const_iterator, string::const_iterator > input)
	: Relation(make_pair(input.first, std::find(input.first, input.second, '[')))
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
		FORIT(architectureFilterIt, architectureFilters)
		{
			string architectureFilter = *architectureFilterIt;
			if (architectureFilter.empty() || architectureFilter[0] != '!')
			{
				warn("non-negative architecture filter '%s'", architectureFilter.c_str());
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
		FORIT(architectureFilterIt, architectureFilters)
		{
			const string& architectureFilter = *architectureFilterIt;
			if (internal::architectureMatch(currentArchitecture, architectureFilter))
			{
				return true; // our case
			}
		}
		return false;
	}
}

RelationLine unarchitectureRelationLine(const ArchitecturedRelationLine& source,
		const string& currentArchitecture)
{
	RelationLine result;

	FORIT(architecturedRelationExpressionIt, source)
	{
		RelationExpression newRelationExpression;

		FORIT(architecturedRelationIt, *architecturedRelationExpressionIt)
		{
			const ArchitecturedRelation& architecturedRelation = *architecturedRelationIt;
			if (__is_architectured_relation_eligible(architecturedRelation, currentArchitecture))
			{
				newRelationExpression.push_back(Relation(architecturedRelation));
			}
		}

		if (!newRelationExpression.empty())
		{
			result.push_back(newRelationExpression);
		}
	}

	return result;
}

string RelationExpression::getHashString() const
{
	char buf[1024] = {0};
	uint16_t bufSize = 0;
	FORIT(relationIt, *this)
	{
		const Relation& relation = *relationIt;

		if (bufSize) // not a start
		{
			if (bufSize + 1u >= sizeof(buf))
			{
				return string();
			}
			buf[bufSize] = '|';
			++bufSize;
		}

		auto packageNameSize = relation.packageName.size();
		if (bufSize + packageNameSize >= sizeof(buf))
		{
			return string();
		}
		strcpy(buf + bufSize, relation.packageName.c_str());
		bufSize += packageNameSize;

		if (relation.relationType != Relation::Types::None)
		{
			auto versionStringSize = relation.versionString.size();
			if (bufSize + versionStringSize + 3 >= sizeof(buf))
			{
				return string();
			}
			buf[bufSize] = ' ';
			++bufSize;
			buf[bufSize] = ('0' + relation.relationType);
			++bufSize;
			buf[bufSize] = ' ';
			++bufSize;
			strcpy(buf + bufSize, relation.versionString.c_str());
			bufSize += versionStringSize;
		}
	}
	return string(buf, bufSize);
}


// yes, I know about templates, but here they cause just too much trouble
#define DEFINE_RELATION_EXPRESSION_CLASS(RelationExpressionType, UnderlyingElement) \
void RelationExpressionType::__init(string::const_iterator begin, string::const_iterator end) \
{ \
	for (auto it = begin; it != end; ++it) \
	{ \
		if (*it == '|') \
		{ \
			/* split OR groups */ \
			auto callback = [this](string::const_iterator begin, string::const_iterator end) \
			{ \
				this->push_back(UnderlyingElement(make_pair(begin, end))); \
			}; \
			internal::processSpacePipeSpaceDelimitedStrings(begin, end, callback); \
			return; \
		} \
	} \
 \
	/* if we reached here, we didn't find OR groups */ \
	push_back(UnderlyingElement(make_pair(begin, end))); \
} \
 \
RelationExpressionType::RelationExpressionType() \
{} \
 \
RelationExpressionType::RelationExpressionType(pair< string::const_iterator, string::const_iterator > input) \
{ \
	__init(input.first, input.second); \
} \
 \
RelationExpressionType::RelationExpressionType(const string& expression) \
{ \
	__init(expression.begin(), expression.end()); \
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
void RelationLineType::__init(string::const_iterator begin, string::const_iterator end) \
{ \
	auto callback = [this](string::const_iterator begin, string::const_iterator end) \
	{ \
		this->push_back(UnderlyingElement(make_pair(begin, end))); \
	}; \
 \
	internal::processSpaceCommaSpaceDelimitedStrings(begin, end, callback); \
} \
 \
RelationLineType::RelationLineType(pair< string::const_iterator, string::const_iterator > input) \
{ \
	__init(input.first, input.second); \
} \
 \
RelationLineType::RelationLineType(const string& line) \
{ \
	__init(line.begin(), line.end()); \
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

