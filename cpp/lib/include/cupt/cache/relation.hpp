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
struct CUPT_API Relation
{
 private:
	CUPT_LOCAL bool __parse_versioned_info(string::const_iterator, string::const_iterator);
	CUPT_LOCAL void __init(string::const_iterator, string::const_iterator);
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
	explicit Relation(const string& input);
	/// constructor
	/**
	 * Parses @a input and constructs Relation from it.
	 * @param input pair of begin iterator and end iterator of stringified relation
	 */
	explicit Relation(pair< string::const_iterator, string::const_iterator > input);
	Relation(Relation&&) = default;
	Relation(const Relation&) = default;
	Relation& operator=(Relation&&) = default;
	Relation& operator=(const Relation&) = default;
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
struct CUPT_API ArchitecturedRelation: public Relation
{
 private:
	CUPT_LOCAL void __init(string::const_iterator, string::const_iterator);
 public:
	/// architecture filters
	vector< string > architectureFilters;

	/// constructor
	/**
	 * Parses @a input and constructs ArchitecturedRelation from it.
	 * @param input stringified architectured relation
	 */
	explicit ArchitecturedRelation(const string& input);
	/// constructor
	/**
	 * Parses @a input and constructs ArchitecturedRelation from it.
	 * @param input pair of begin iterator and end iterator of stringified
	 * architectured relation
	 */
	explicit ArchitecturedRelation(pair< string::const_iterator, string::const_iterator > input);
	ArchitecturedRelation(ArchitecturedRelation&&) = default;
	ArchitecturedRelation(const ArchitecturedRelation&) = default;
	ArchitecturedRelation& operator=(ArchitecturedRelation&&) = default;
	ArchitecturedRelation& operator=(const ArchitecturedRelation&) = default;
	string toString() const;
};

/// group of alternative relations
struct CUPT_API RelationExpression: public vector< Relation >
{
 private:
	CUPT_LOCAL void __init(string::const_iterator, string::const_iterator);
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
	explicit RelationExpression(const string& input);
	/// constructor
	/**
	 * @param input pair of begin iterator and end iterator of string
	 * representation
	 */
	explicit RelationExpression(pair< string::const_iterator, string::const_iterator > input);
	RelationExpression(RelationExpression&&) = default;
	RelationExpression(const RelationExpression&) = default;
	RelationExpression& operator=(RelationExpression&&) = default;
	RelationExpression& operator=(const RelationExpression&) = default;
	/// destructor
	virtual ~RelationExpression();
};

/// group of alternative architectured relation expressions
struct CUPT_API ArchitecturedRelationExpression: public vector< ArchitecturedRelation >
{
 private:
	CUPT_LOCAL void __init(string::const_iterator, string::const_iterator);
 public:
	/// gets the string representation
	string toString() const;
	/// default constructor
	/**
	 * Builds ArchitecturedRelationExpression containing no relations.
	 */
	ArchitecturedRelationExpression();
	/// constructor
	/**
	 * @param input string representation
	 */
	explicit ArchitecturedRelationExpression(const string& input);
	/// constructor
	/**
	 * @param input pair of begin iterator and end iterator of string
	 * representation
	 */
	explicit ArchitecturedRelationExpression(pair< string::const_iterator, string::const_iterator > input);
	ArchitecturedRelationExpression(ArchitecturedRelationExpression&&) = default;
	ArchitecturedRelationExpression(const ArchitecturedRelationExpression&) = default;
	ArchitecturedRelationExpression& operator=(ArchitecturedRelationExpression&&) = default;
	ArchitecturedRelationExpression& operator=(const ArchitecturedRelationExpression&) = default;
	/// destructor
	virtual ~ArchitecturedRelationExpression();
};

/// array of relation expressions
struct CUPT_API RelationLine: public vector< RelationExpression >
{
 private:
	CUPT_LOCAL void __init(string::const_iterator, string::const_iterator);
 public:
	/// gets the string representation
	string toString() const;
	/// default constructor
	/**
	 * Builds RelationLine containing no relation expressions.
	 */
	RelationLine();
	/// constructor
	/**
	 * @param input string representation
	 */
	explicit RelationLine(const string& input);
	/// constructor
	/**
	 * @param input pair of begin iterator and end iterator of string
	 * representation
	 */
	explicit RelationLine(pair< string::const_iterator, string::const_iterator > input);
	RelationLine(RelationLine&&) = default;
	RelationLine(const RelationLine&) = default;
	RelationLine& operator=(RelationLine&&) = default;
	RelationLine& operator=(const RelationLine&) = default;
	/// destructor
	virtual ~RelationLine();
};

/// array of architectured relation expressions
struct CUPT_API ArchitecturedRelationLine: public vector< ArchitecturedRelationExpression >
{
 private:
	CUPT_LOCAL void __init(string::const_iterator, string::const_iterator);
 public:
	/// gets the string representation
	string toString() const;
	/// default constructor
	/**
	 * Builds RelationLine containing no architectured relation expressions.
	 */
	ArchitecturedRelationLine();
	/// constructor
	/**
	 * @param input string representation
	 */
	explicit ArchitecturedRelationLine(const string& input);
	/// constructor
	/**
	 * @param input pair of begin iterator and end iterator of string
	 * representation
	 */
	explicit ArchitecturedRelationLine(pair< string::const_iterator, string::const_iterator > input);
	ArchitecturedRelationLine(ArchitecturedRelationLine&&) = default;
	ArchitecturedRelationLine(const ArchitecturedRelationLine&) = default;
	ArchitecturedRelationLine& operator=(ArchitecturedRelationLine&&) = default;
	ArchitecturedRelationLine& operator=(const ArchitecturedRelationLine&) = default;
	/// converts to RelationLine given system architecture
	/**
	 * Filters ArchitecturedRelationLine using binary system architecture.
	 * Throws out architectured relation expressions, where @ref
	 * ArchitecturedRelation::architectureFilters do not match system architecture. Matching
	 * architectured relation expressions are converted to relation
	 * expressions.
	 * @param currentArchitecture system binary architetecture
	 * @return relation line
	 */
	RelationLine toRelationLine(const string& currentArchitecture) const;
	/// destructor
	virtual ~ArchitecturedRelationLine();
};

}
}

#endif

