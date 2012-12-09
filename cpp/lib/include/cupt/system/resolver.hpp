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
#ifndef CUPT_COMMON_RESOLVER_SEEN
#define CUPT_COMMON_RESOLVER_SEEN

/// @file

#include <functional>

#include <cupt/common.hpp>
#include <cupt/cache/binaryversion.hpp>

namespace cupt {
namespace system {

using namespace cache;

/// dependency problems resolver
/**
 * This class provides the dependency problems resolver interface for system state
 * modifications.
 *
 * First, you call class methods to specify how would you want to modify the
 * system, and then you finally call @ref resolve to get a consistent package
 * set(s) for specified modifications.
 */
class CUPT_API Resolver
{
	Resolver(const Resolver&);
	Resolver& operator=(const Resolver&);
 public:
	/// base class for resolver decision reasons
	struct Reason
	{
	 protected:
		CUPT_LOCAL Reason() {};
	 public:
		virtual ~Reason() {}; // polymorphic
		virtual string toString() const = 0; ///< returns localized reason description
	};
	/// reason: asked by user
	/**
	 * This reason means that change was asked by "user" by calling @ref
	 * installVersion, @ref removePackage etc. methods.
	 */
	struct UserReason: public Reason
	{
		virtual string toString() const;
	};
	/// reason: auto-removal
	/**
	 * This reason applies only to package removals. It means that resolver
	 * decided to remove the package since it's automatically installed and no
	 * manually installed packages or their dependencies depend on this package
	 * anymore.
	 */
	struct AutoRemovalReason: public Reason
	{
		virtual string toString() const;
	};
	/// reason: other version's dependency
	/**
	 * This reason means that a resolver decided to change a package state
	 * because of some dependency of another package version.
	 */
	struct RelationExpressionReason: public Reason
	{
		const BinaryVersion* version; ///< version that caused the change
		BinaryVersion::RelationTypes::Type dependencyType; ///< type of dependency that caused the change
		RelationExpression relationExpression; ///< relation expression which caused the change

		/// trivial constructor
		RelationExpressionReason(const BinaryVersion*,
				BinaryVersion::RelationTypes::Type, const RelationExpression&);
		virtual string toString() const;
	};
	/// reason: source-synchronized with a related binary package
	/**
	 * This reason means that synchronizing by source versions was enabled and
	 * this package was synchronized to the version of other binary package
	 * from the same source.
	 */
	struct SynchronizationReason: public Reason
	{
		const BinaryVersion* version; ///< version that caused the change
		string relatedPackageName; ///< name of related binary package

		/// trivial constructor
		SynchronizationReason(const BinaryVersion*, const string&);
		virtual string toString() const;
	};

	/// resolver's main solution item
	/**
	 * Represents a binary package in the suggested system.
	 */
	struct SuggestedPackage
	{
		const BinaryVersion* version; ///< package version
		bool automaticallyInstalledFlag;
		vector< shared_ptr< const Reason > > reasons; ///< list of resolver reasons if tracked
		vector< string > reasonPackageNames; ///< changes in these packages caused the change in this package
	};
	typedef map< string, SuggestedPackage > SuggestedPackages; ///< suggested set of packages
	/// the result of resolver's work
	struct Offer
	{
		SuggestedPackages suggestedPackages; ///< target system package set
		vector< shared_ptr< const Reason > > unresolvedProblems;
	};

	/// user callback answer variants
	struct UserAnswer
	{
		enum Type
		{
			Accept, ///< finish computations and return @c true
			Decline, ///< throw out the proposed solution and work on other ones
			Abandon ///< finish computations and return @c false
		};
	};

	/// callback function type
	typedef std::function< UserAnswer::Type (const Offer&) > CallbackType;

	Resolver() {};

	/**
	 * Requests installation of the specific version.
	 */
	virtual void installVersion(const BinaryVersion*) = 0;
	/**
	 * Requests that specified relation expression is satisfied.
	 */
	virtual void satisfyRelationExpression(const RelationExpression&) = 0;
	/**
	 * Requests that specified relation expression is not satisfied.
	 */
	virtual void unsatisfyRelationExpression(const RelationExpression&) = 0;
	/**
	 * Requests that specified package is removed.
	 *
	 * @param packageName
	 */
	virtual void removePackage(const string& packageName) = 0;
	/**
	 * Requests an upgrade of all installed packages (to their preferred version).
	 */
	virtual void upgrade() = 0;
	/**
	 * Requests that if a solution will have the package @a packageName,
	 * its corresponding Offer::SuggestedPackage::automaticallyInstalledFlag
	 * will have the value of @a flagValue.
	 */
	virtual void setAutomaticallyInstalledFlag(const string& packageName, bool flagValue) = 0;

	/// perform a resolve computations
	/**
	 * Takes all requested data and tries to find the best valid set of
	 * packages which conforms to what was requested.
	 *
	 * @return @c true if the solution was found and accepted by user, @c false otherwise
	 */
	virtual bool resolve(CallbackType) = 0;

	/// destructor
	virtual ~Resolver() {};
};

}
}

#endif

