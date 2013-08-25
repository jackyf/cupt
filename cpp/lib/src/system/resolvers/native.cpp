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
#include <cupt/system/resolvers/native.hpp>

#include <internal/nativeresolver/impl.hpp>

namespace cupt {
namespace system {

NativeResolver::NativeResolver(const shared_ptr< const Config >& config,
		const shared_ptr< const Cache >& cache)
	: __impl(new internal::NativeResolverImpl(config, cache))
{}

NativeResolver::~NativeResolver()
{
	delete __impl;
}

void NativeResolver::satisfyRelationExpression(const RelationExpression& relationExpression,
		bool invert, const string& annotation)
{
	__impl->satisfyRelationExpression(relationExpression, invert, annotation);
}

void NativeResolver::upgrade()
{
	__impl->upgrade();
}

void NativeResolver::setAutomaticallyInstalledFlag(const string& packageName, bool flagValue)
{
	__impl->setAutomaticallyInstalledFlag(packageName, flagValue);
}

bool NativeResolver::resolve(Resolver::CallbackType callback)
{
	return __impl->resolve(callback);
}

}
}

