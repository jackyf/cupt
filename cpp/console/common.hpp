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

#ifndef COMMON_SEEN
#define COMMON_SEEN

#include <unordered_map>
using std::unordered_map;
#include <list>
using std::list;

#include <cupt/common.hpp>
#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/binaryversion.hpp>

using namespace cupt;
using namespace cupt::cache;
using namespace cupt::system;
using namespace cupt::download;

class ReverseDependsIndex
{
	typedef unordered_map< PackageId, list< const BinaryPackage* > > PerRelationType;

	const Cache& __cache;
	map< BinaryVersion::RelationTypes::Type, PerRelationType > __data;

	void __add(BinaryVersion::RelationTypes::Type relationType, PerRelationType*);
 public:
	ReverseDependsIndex(const Cache& cache);
	void add(BinaryVersion::RelationTypes::Type relationType);

	void foreachReverseDependency(
			const BinaryVersion* version, BinaryVersion::RelationTypes::Type relationType,
			const std::function< void (const BinaryVersion*, const RelationExpression&) > callback);
};

bool isPackageInstalled(const Cache&, const string& packageName);


#endif

