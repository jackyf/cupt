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
class Resolver
{
	Resolver(const Resolver&);
	Resolver& operator=(const Resolver&);
 public:
	struct Reason
	{
	 protected:
		Reason() {};
	 public:
		virtual ~Reason() {}; // polymorphic
	};
	struct UserReason: public Reason
	{};
	struct AutoRemovalReason: public Reason
	{};
	struct RelationExpressionReason: public Reason
	{
		shared_ptr< const BinaryVersion > version;
		BinaryVersion::RelationTypes::Type dependencyType;
		cache::RelationExpression relationExpression;

		RelationExpressionReason(const shared_ptr< const BinaryVersion >& version_,
				BinaryVersion::RelationTypes::Type dependencyType_,
				const cache::RelationExpression& relationExpression_)
			: version(version_), dependencyType(dependencyType_),
			relationExpression(relationExpression_) {}
	};
	struct SynchronizationReason: public Reason
	{
		string packageName;

		SynchronizationReason(const string& packageName_)
			: packageName(packageName_) {}
	};

	struct SuggestedPackage
	{
		shared_ptr< const BinaryVersion > version;
		bool manuallySelected;
		vector< shared_ptr< const Reason > > reasons;
	};
	typedef map< string, SuggestedPackage > SuggestedPackages;

	struct UserAnswer
	{
		enum Type { Accept, Decline, Abandon };
	};

	typedef std::function< UserAnswer::Type (const SuggestedPackages&) > CallbackType;

	Resolver() {};

	virtual void installVersion(const shared_ptr< const BinaryVersion >&) = 0;
	virtual void satisfyRelationExpression(const RelationExpression&) = 0;
	virtual void unsatisfyRelationExpression(const RelationExpression&) = 0;
	virtual void removePackage(const string& packageName) = 0;
	virtual void upgrade() = 0;

	virtual bool resolve(CallbackType) = 0;

	virtual ~Resolver() {};
};

}
}

#endif

