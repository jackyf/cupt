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
#ifndef CUPT_SYSTEM_RESOLVERS_NATIVE
#define CUPT_SYSTEM_RESOLVERS_NATIVE

/// @file

#include <cupt/system/resolver.hpp>
#include <cupt/cache/relation.hpp>

namespace cupt {

namespace internal {

class NativeResolverImpl;

}

namespace system {

/// library's problem resolver implementation
class CUPT_API NativeResolver: public Resolver
{
	internal::NativeResolverImpl* __impl;

 public:
	/// constructor
	NativeResolver(const shared_ptr< const Config >&, const shared_ptr< const Cache >&);

	void installVersion(const BinaryVersion*);
	void satisfyRelationExpression(const RelationExpression&);
	void unsatisfyRelationExpression(const RelationExpression&);
	void removePackage(const string& packageName);
	void upgrade();

	bool resolve(Resolver::CallbackType);

	~NativeResolver();
};

}
}

#endif

