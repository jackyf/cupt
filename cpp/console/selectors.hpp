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

#ifndef SELECTORS_SEEN
#define SELECTORS_SEEN

#include <cupt/cache/binaryversion.hpp>
#include <cupt/cache/binarypackage.hpp>

#include "common.hpp"

shared_ptr< const BinaryPackage > getBinaryPackage(shared_ptr< const Cache > cache,
		const string& packageName, bool throwOnError = true);
shared_ptr< const SourcePackage > getSourcePackage(shared_ptr< const Cache > cache,
		const string& packageName, bool throwOnError = true);
shared_ptr< const BinaryVersion > selectBinaryVersion(shared_ptr< const Cache > cache,
		const string& packageExpression, bool throwOnError);
vector< shared_ptr< const BinaryVersion > > selectBinaryVersionsWildcarded(shared_ptr< const Cache > cache,
		const string& packageExpression, bool throwOnError = true);
vector< shared_ptr< const SourceVersion > > selectSourceVersionsWildcarded(shared_ptr< const Cache > cache,
		const string& packageExpression, bool throwOnError = true);

#endif

