/**************************************************************************
*   Copyright (C) 2010-2011 by Eugene V. Lyubimkin                        *
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
#ifndef CUPT_INTERNAL_NATIVERESOLVER_DEPENDENCY_GRAPH_SEEN
#define CUPT_INTERNAL_NATIVERESOLVER_DEPENDENCY_GRAPH_SEEN

#include <map>
#include <forward_list>
using std::forward_list;

#include <cupt/common.hpp>
#include <cupt/fwd.hpp>
#include <cupt/system/resolver.hpp>
typedef cupt::system::Resolver::Reason Reason;
using cupt::cache::BinaryVersion;

#include <internal/graph.hpp>

namespace cupt {
namespace internal {

class PackageEntry;

namespace dependencygraph {

struct InitialPackageEntry
{
	shared_ptr< const BinaryVersion > version;
	bool sticked;
	bool modified;

	InitialPackageEntry();
};

struct Unsatisfied
{
	enum Type { None, Recommends, Suggests, Sync };
};

struct BasicVertex;
typedef BasicVertex Element;
struct BasicVertex
{
 private:
	static uint32_t __next_id;
 public:
	const uint32_t id;
	virtual string toString() const = 0;
	virtual size_t getTypePriority() const;
	virtual shared_ptr< const Reason > getReason(const BasicVertex& parent) const;
	virtual bool isAnti() const;
	virtual const forward_list< const Element* >* getRelatedElements() const;
	virtual Unsatisfied::Type getUnsatisfiedType() const;

	BasicVertex();
	virtual ~BasicVertex();
};
struct VersionVertex: public BasicVertex
{
 private:
	const map< string, forward_list< const Element* > >::iterator __related_element_ptrs_it;
 public:
	shared_ptr< const BinaryVersion > version;

	VersionVertex(const map< string, forward_list< const Element* > >::iterator&);
	string toString() const;
	const forward_list< const Element* >* getRelatedElements() const;
	const string& getPackageName() const;
	string toLocalizedString() const;
};
typedef VersionVertex VersionElement;

namespace {

template< class T >
struct PointeredAlreadyTraits
{
	typedef T PointerType;
	static T toPointer(T vertex)
	{
		return vertex;
	}
};

}

class DependencyGraph: protected Graph< const Element*, PointeredAlreadyTraits >
{
	const Config& __config;
	const Cache& __cache;

	class FillHelper;
	friend class FillHelper;

	std::unique_ptr< FillHelper > __fill_helper;
 public:
	typedef Graph< const Element*, PointeredAlreadyTraits > BaseT;

	DependencyGraph(const Config& config, const Cache& cache);
	~DependencyGraph();
	vector< pair< const Element*, shared_ptr< const PackageEntry > > > fill(
			const map< string, shared_ptr< const BinaryVersion > >&,
			const map< string, InitialPackageEntry >&);

	const Element* getCorrespondingEmptyElement(const Element*);
	void unfoldElement(const Element*);

	using BaseT::getSuccessorsFromPointer;
	using BaseT::getPredecessorsFromPointer;
	using BaseT::CessorListType;
};

}
}
}

#endif

