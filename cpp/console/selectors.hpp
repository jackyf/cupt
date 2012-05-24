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

const BinaryPackage* getBinaryPackage(const Cache& cache,
		const string& packageName, bool throwOnError = true);
const SourcePackage* getSourcePackage(const Cache& cache,
		const string& packageName, bool throwOnError = true);
const BinaryVersion* selectBinaryVersion(const Cache& cache,
		const string& packageExpression, bool throwOnError);
vector< const BinaryVersion* > selectBinaryVersionsWildcarded(const Cache& cache,
		const string& packageExpression, bool throwOnError = true);
vector< const SourceVersion* > selectSourceVersionsWildcarded(const Cache& cache,
		const string& packageExpression, bool throwOnError = true);
vector< const BinaryVersion* > selectAllBinaryVersionsWildcarded(const Cache& cache,
		const string& packageExpression);
vector< const SourceVersion* > selectAllSourceVersionsWildcarded(const Cache& cache,
		const string& packageExpression);

#endif

