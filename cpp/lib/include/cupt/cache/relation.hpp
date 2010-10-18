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
#ifndef CUPT_CACHE_RELATION_SEEN
#define CUPT_CACHE_RELATION_SEEN

/// @file

#include <cupt/common.hpp>

namespace cupt {
namespace cache {

/// %relation against certain binary package
struct Relation
{
 private:
	bool __parse_versioned_info(string::const_iterator, string::const_iterator);
	void __init(string::const_iterator, string::const_iterator);
 public:
	/// %relation type
	struct Types
	{
		/// type
		enum Type { Less, Equal, More, LessOrEqual, MoreOrEqual, None };
		/// string values of corresponding types
		static const string strings[];
	};
	string packageName; ///< package name
	Types::Type relationType; ///< relation type
	string versionString; ///< version string

	/// constructor
	/**
	 * Parses @a input and constructs Relation from it.
	 * @param input stringified relation
	 */
	Relation(const string& input);
	/// constructor
	/**
	 * Parses @a input and constructs Relation from it.
	 * @param input pair of begin iterator and end iterator of stringified relation
	 */
	Relation(pair< string::const_iterator, string::const_iterator > input);
	/// destructor
	virtual ~Relation();
	/// gets the string reprentation
	string toString() const;
	/// is relation satisfied by @a otherVersionString
	/**
	 * This method checks @ref relationType and @ref versionString against @a
	 * otherVersionString.
	 *
	 * @param otherVersionString version string to compare
	 * @return @c true if satisfied, @c false if not
	 */
	bool isSatisfiedBy(const string& otherVersionString) const;
	/// operator ==
	/**
	 * @param other relation to compare with
	 * @return @c true if this relation is equal to @a other, @c false otherwise
	 */
	bool operator==(const Relation& other) const;
};

/// relation with optional architecture filters
struct ArchitecturedRelation: public Relation
{
 private:
	void __init(string::const_iterator, string::const_iterator);
 public:
	/// architecture filters
	vector< string > architectureFilters;

	/// constructor
	/**
	 * Parses @a input and constructs ArchitecturedRelation from it.
	 * @param input stringified architectured relation
	 */
	ArchitecturedRelation(const string& input);
	/// constructor
	/**
	 * Parses @a input and constructs ArchitecturedRelation from it.
	 * @param input pair of begin iterator and end iterator of stringified
	 * architectured relation
	 */
	ArchitecturedRelation(pair< string::const_iterator, string::const_iterator > input);
	string toString() const;
};

/// group of alternative relations
struct RelationExpression: public vector< Relation >
{
 private:
	void __init(string::const_iterator, string::const_iterator);
 public:
	/// gets the string representation
	string toString() const;
	/// fast function to get unique, not human-readable identifier
	string getHashString() const;
	/// default constructor
	/**
	 * Builds RelationExpression containing no relations.
	 */
	RelationExpression();
	/// constructor
	/**
	 * @param input string representation
	 */
	RelationExpression(const string& input);
	/// constructor
	/**
	 * @param input pair of begin iterator and end iterator of string
	 * representation
	 */
	RelationExpression(pair< string::const_iterator, string::const_iterator > input);
	/// destructor
	virtual ~RelationExpression();
};

/// group of alternative architectured relation expressions
struct ArchitecturedRelationExpression: public vector< ArchitecturedRelation >
{
 private:
	void __init(string::const_iterator, string::const_iterator);
 public:
	/// gets the string representation
	string toString() const;
	string getHashString() const;
	ArchitecturedRelationExpression();
	ArchitecturedRelationExpression(const string&);
	ArchitecturedRelationExpression(pair< string::const_iterator, string::const_iterator >);
	virtual ~ArchitecturedRelationExpression();
};

struct RelationLine: public vector< RelationExpression >
{
 private:
	void __init(string::const_iterator, string::const_iterator);
 public:
	string toString() const;
	RelationLine();
	RelationLine(const string&);
	RelationLine(pair< string::const_iterator, string::const_iterator >);
	virtual ~RelationLine();
};

struct ArchitecturedRelationLine: public vector< ArchitecturedRelationExpression >
{
 private:
	void __init(string::const_iterator, string::const_iterator);
 public:
	string toString() const;
	ArchitecturedRelationLine();
	ArchitecturedRelationLine(const string&);
	ArchitecturedRelationLine(pair< string::const_iterator, string::const_iterator >);
	RelationLine toRelationLine(const string& currentArchitecture) const;
	virtual ~ArchitecturedRelationLine();
};

}
}

#endif

