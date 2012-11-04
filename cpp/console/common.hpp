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
#include <cupt/cache/sourceversion.hpp>

using namespace cupt;
using namespace cupt::cache;
using namespace cupt::system;
using namespace cupt::download;

template < typename T > struct VersionTraits;
template<> struct VersionTraits< BinaryVersion >
{
	typedef BinaryPackage PackageT;
	typedef BinaryVersion::RelationTypes::Type RelationTypeT;
};
template<> struct VersionTraits< SourceVersion >
{
	typedef SourcePackage PackageT;
	typedef SourceVersion::RelationTypes::Type RelationTypeT;
};

template < typename VersionT >
class ReverseDependsIndex
{
	typedef VersionTraits< VersionT > VT;
	typedef typename VT::PackageT PackageT;
	typedef typename VT::RelationTypeT RelationTypeT;

	typedef unordered_map< string, list< const PackageT* > > PerRelationType;

	const Cache& __cache;
	map< RelationTypeT, PerRelationType > __data;
	const string __architecture;

	void __add(RelationTypeT relationType, PerRelationType*);
	const RelationLine& __getRelationLine(const RelationLine&) const;
	RelationLine __getRelationLine(const ArchitecturedRelationLine&) const;
 public:
	ReverseDependsIndex(const Cache&);
	void add(RelationTypeT relationType);

	void foreachReverseDependency(
			const BinaryVersion* version, RelationTypeT relationType,
			const std::function< void (const VersionT*, const RelationExpression&) > callback);
};

bool isPackageInstalled(const Cache&, const string& packageName);


#endif

