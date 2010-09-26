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

#include <functional>

#include <cupt/common.hpp>
#include <cupt/cache/binaryversion.hpp>

namespace cupt {
namespace system {

using namespace cache;

class Resolver
{
	Resolver(const Resolver&);
 public:
	struct Reason
	{
		enum Type { User, AutoRemoval, RelationExpression, Synchronization };

		Type type;

		// relation expression part
		shared_ptr< const BinaryVersion > version;
		BinaryVersion::RelationTypes::Type dependencyType;
		cache::RelationExpression relationExpression;
		// synchronization part
		string packageName;

		Reason(Type type_)
			: type(type_), relationExpression("fake") {}
		Reason(const shared_ptr< const BinaryVersion >& version_,
				BinaryVersion::RelationTypes::Type dependencyType_,
				const cache::RelationExpression& relationExpression_)
			: type(RelationExpression), version(version_),
			dependencyType(dependencyType_),
			relationExpression(relationExpression_) {}
		Reason(const string& packageName_)
			: type(Synchronization), relationExpression("fake"), packageName(packageName_) {}
	};

	struct SuggestedPackage
	{
		shared_ptr< const BinaryVersion > version;
		bool manuallySelected;
		vector< Reason > reasons;
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

