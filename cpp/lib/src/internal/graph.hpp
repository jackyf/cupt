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
#ifndef CUPT_INTERNAL_GRAPH_SEEN
#define CUPT_INTERNAL_GRAPH_SEEN

#include <set>
#include <unordered_set>
#include <map>
#include <vector>
#include <queue>
#include <algorithm>

#include <cupt/common.hpp>

namespace cupt {
namespace internal {

using std::map;
using std::set;
using std::unordered_set;
using std::queue;
using std::priority_queue;

namespace {

template< class T >
struct DefaultPointerTraits
{
	typedef const T* PointerType;
	static PointerType toPointer(const T& vertex)
	{
		return &vertex;
	}
};

}

template < class T, template < class X > class PtrTraitsT = DefaultPointerTraits >
class Graph
{
	typedef typename PtrTraitsT< T >::PointerType PtrT;
 public:
	typedef std::vector< PtrT > CessorListType; // type for {suc,prede}cessor lists
 private:
	set< T > __vertices;
	mutable map< PtrT, CessorListType > __predecessors;
	mutable map< PtrT, CessorListType > __successors;

	static const CessorListType __null_list;

 public:
	const set< T >& getVertices() const;
	vector< pair< PtrT, PtrT > > getEdges() const;

	bool hasEdgeFromPointers(PtrT fromVertexPtr, PtrT toVertexPtr) const;

	const CessorListType& getPredecessorsFromPointer(PtrT vertexPtr) const;
	const CessorListType& getSuccessorsFromPointer(PtrT vertexPtr) const;


	PtrT addVertex(const T& vertex);
	void deleteVertex(const T& vertex);

	void addEdgeFromPointers(PtrT fromVertexPtr, PtrT toVertexPtr);
	void deleteEdgeFromPointers(PtrT fromVertexPtr, PtrT toVertexPtr);

	unordered_set< PtrT > getReachableFrom(const T& from) const;
	unordered_set< PtrT > getReachableTo(const T& to) const;

	template< class PriorityLess, class OutputIterator >
	void topologicalSortOfStronglyConnectedComponents(
			std::function< void (const vector< T >&, bool) > callback,
			OutputIterator outputIterator) const;
};

template< class T, template < class X > class PtrTraitsT >
const typename Graph< T, PtrTraitsT >::CessorListType Graph< T, PtrTraitsT >::__null_list;

template< class T, template < class X > class PtrTraitsT >
const set< T >& Graph< T, PtrTraitsT >::getVertices() const
{
	return __vertices;
}

template< class T, template < class X > class PtrTraitsT >
auto Graph< T, PtrTraitsT >::getEdges() const -> vector< pair< PtrT, PtrT > >
{
	vector< pair< PtrT, PtrT > > result;
	FORIT(vertexIt, __vertices)
	{
		PtrT vertexPtr = &*vertexIt;

		const CessorListType& predecessors = getPredecessorsFromPointer(vertexPtr);
		FORIT(predecessorPtrIt, predecessors)
		{
			result.push_back(std::make_pair(*predecessorPtrIt, vertexPtr));
		}
	}

	return result;
}

template< class T, template < class X > class PtrTraitsT >
auto Graph< T, PtrTraitsT >::addVertex(const T& vertex) -> PtrT
{
	return PtrTraitsT< T >::toPointer(*__vertices.insert(vertex).first);
}

template< class ContainerT, class ElementT >
void __remove_from_cessors(ContainerT& cessors, const ElementT& vertexPtr)
{
	FORIT(it, cessors)
	{
		if (*it == vertexPtr)
		{
			cessors.erase(it);
			return;
		}
	}
	fatal2("internal error: graph: the vertex was not found while deleting from cessors list");
}

template< class T, template < class X > class PtrTraitsT >
void Graph< T, PtrTraitsT >::deleteVertex(const T& vertex)
{
	// searching for vertex
	auto it = __vertices.find(vertex);
	if (it != __vertices.end())
	{
		auto vertexPtr = PtrTraitsT< T >::toPointer(*it);
		// deleting edges containing vertex
		const CessorListType& predecessors = getPredecessorsFromPointer(vertexPtr);
		const CessorListType& successors = getSuccessorsFromPointer(vertexPtr);
		FORIT(predecessorIt, predecessors)
		{
			__remove_from_cessors(__successors[*predecessorIt], vertexPtr);
		}
		__predecessors.erase(vertexPtr);

		FORIT(successorIt, successors)
		{
			__remove_from_cessors(__predecessors[*successorIt], vertexPtr);
		}
		__successors.erase(vertexPtr);

		// and the vertex itself
		__vertices.erase(it);
	}
}

template< class T, template < class X > class PtrTraitsT >
bool Graph< T, PtrTraitsT >::hasEdgeFromPointers(PtrT fromVertexPtr, PtrT toVertexPtr) const
{
	const CessorListType& predecessors = getPredecessorsFromPointer(toVertexPtr);
	FORIT(vertexPtrIt, predecessors)
	{
		if (fromVertexPtr == *vertexPtrIt)
		{
			return true;
		}
	}
	return false;
}

template< class T, template < class X > class PtrTraitsT >
void Graph< T, PtrTraitsT >::addEdgeFromPointers(PtrT fromVertexPtr, PtrT toVertexPtr)
{
	if (!hasEdgeFromPointers(fromVertexPtr, toVertexPtr))
	{
		__predecessors[toVertexPtr].push_back(fromVertexPtr);
		__successors[fromVertexPtr].push_back(toVertexPtr);
	}
}

template< class T, template < class X > class PtrTraitsT >
void Graph< T, PtrTraitsT >::deleteEdgeFromPointers(PtrT fromVertexPtr, PtrT toVertexPtr)
{
	auto predecessorsIt = __predecessors.find(toVertexPtr);
	auto successorsIt = __successors.find(fromVertexPtr);
	if (predecessorsIt != __predecessors.end())
	{
		__remove_from_cessors(predecessorsIt->second, fromVertexPtr);
	}
	if (successorsIt != __successors.end())
	{
		__remove_from_cessors(successorsIt->second, toVertexPtr);
	}
}

template< class T, template < class X > class PtrTraitsT >
const typename Graph< T, PtrTraitsT >::CessorListType& Graph< T, PtrTraitsT >::getPredecessorsFromPointer(PtrT vertexPtr) const
{
	auto it = __predecessors.find(vertexPtr);
	return (it != __predecessors.end() ? it->second : __null_list);
}

template< class T, template < class X > class PtrTraitsT >
const typename Graph< T, PtrTraitsT >::CessorListType& Graph< T, PtrTraitsT >::getSuccessorsFromPointer(PtrT vertexPtr) const
{
	auto it = __successors.find(vertexPtr);
	return (it != __successors.end() ? it->second : __null_list);
}

template< class T >
void __dfs_visit(const Graph< T >& graph, const T* vertexPtr,
		set< const T* >& seen, vector< const T* >& output)
{
	seen.insert(vertexPtr);

	const typename Graph< T >::CessorListType& successors = graph.getSuccessorsFromPointer(vertexPtr);
	FORIT(toVertexIt, successors)
	{
		if (!seen.count(*toVertexIt))
		{
			__dfs_visit(graph, *toVertexIt, seen, output);
		}
	}

	output.push_back(vertexPtr);
}

template < class T >
vector< const T* > __dfs_mode1(const Graph< T >& graph)
{
	vector< const T* > vertices;

	FORIT(it, graph.getVertices())
	{
		vertices.push_back(&*it);
	}

	set< const T* > seen;
	vector< const T* > result;

	FORIT(vertexIt, vertices)
	{
		if (!seen.count(*vertexIt))
		{
			__dfs_visit(graph, *vertexIt, seen, result);
		}
	}

	return result;
}

template < class T >
vector< vector< const T* > > __dfs_mode2(const Graph< T >& graph, const vector< const T* >& vertices)
{
	set< const T* > seen;
	vector< const T* > stronglyConnectedComponent;

	vector< vector< const T* > > result; // topologically sorted vertices

	FORIT(vertexIt, vertices)
	{
		if (!seen.count(*vertexIt))
		{
			__dfs_visit(graph, *vertexIt, seen, stronglyConnectedComponent);
			result.push_back(std::move(stronglyConnectedComponent));
			stronglyConnectedComponent.clear();
		}
	}

	return result;
}

template < class T >
Graph< vector< T > > __make_scc_graph(const Graph< T >& graph,
		const vector< vector< const T* > >& scc)
{
	Graph< vector< T > > result;

	// indexing original vertices
	map< const T*, const vector< T >* > vertexToComponent;
	vector< const vector< T >* > sccVertexPtrs;
	{
		FORIT(sccIt, scc)
		{
			// converting from (const T*) to T
			vector< T > component;
			FORIT(it, *sccIt)
			{
				component.push_back(**it);
			}

			auto componentPtr = result.addVertex(component);

			FORIT(it, *sccIt)
			{
				vertexToComponent[*it] = componentPtr;
			}
		}
	}

	{ // go check all edges for cross-component ones
		FORIT(vertexIt, graph.getVertices())
		{
			const T* vertexPtr = &*vertexIt;
			auto fromComponentPtr = vertexToComponent[vertexPtr];

			const typename Graph< T >::CessorListType& successors = graph.getSuccessorsFromPointer(vertexPtr);
			FORIT(successorPtrIt, successors)
			{
				auto toComponentPtr = vertexToComponent[*successorPtrIt];
				if (fromComponentPtr != toComponentPtr)
				{
					result.addEdgeFromPointers(fromComponentPtr, toComponentPtr); // cross-component edge
				}
			}
		}
	}

	return result;
}

template < class T, class PriorityLess, class OutputIterator >
void __topological_sort_with_priorities(Graph< vector< T > >&& graph,
		std::function< void (const vector< T >&, bool) > callback,
		OutputIterator outputIterator)
{
	priority_queue< const vector< T >*, vector< const vector< T >* >, PriorityLess > haveNoPredecessors;

	FORIT(vertexIt, graph.getVertices())
	{
		auto vertexPtr = &*vertexIt;
		if (graph.getPredecessorsFromPointer(vertexPtr).empty())
		{
			haveNoPredecessors.push(vertexPtr);
			callback(*vertexPtr, false);
		}
	}

	while (!haveNoPredecessors.empty())
	{
		auto vertexPtr = haveNoPredecessors.top();
		haveNoPredecessors.pop();

		*outputIterator = *vertexPtr;
		++outputIterator;
		callback(*vertexPtr, true);

		const typename Graph< vector< T > >::CessorListType successors =
				graph.getSuccessorsFromPointer(vertexPtr); // list copy, yes
		graph.deleteVertex(*vertexPtr);
		FORIT(successorIt, successors)
		{
			if (graph.getPredecessorsFromPointer(*successorIt).empty())
			{
				haveNoPredecessors.push(*successorIt);
				callback(**successorIt, false);
			}
		}

	}
	if (!graph.getVertices().empty())
	{
		fatal2("internal error: topologic sort of strongly connected components: cycle detected");
	}
}

template< class T, template < class X > class PtrTraitsT >
template < class PriorityLess, class OutputIterator >
void Graph< T, PtrTraitsT >::topologicalSortOfStronglyConnectedComponents(
		std::function< void (const vector< T >&, bool) > callback,
		OutputIterator outputIterator) const
{
	auto vertices = __dfs_mode1(*this);

	// transposing the graph temporarily
	__successors.swap(__predecessors);

	std::reverse(vertices.begin(), vertices.end());

	auto scc = __dfs_mode2(*this, vertices);

	// transposing it to original state
	__successors.swap(__predecessors);

	// now, it would be easy to return the result from __dfs_mode2, since it
	// returns the strongly connected components in topological order already,
	// but we want to take vertex priorities in the account so we need a
	// strongly connected graph for it
	__topological_sort_with_priorities< T, PriorityLess >(
			__make_scc_graph(*this, scc), callback, outputIterator);
}

template < class T, template < class X > class PtrTraitsT >
auto Graph< T, PtrTraitsT >::getReachableFrom(const T& from) const -> unordered_set< PtrT >
{
	auto it = __vertices.find(from);
	if (it == __vertices.end())
	{
		return unordered_set< PtrT >();
	}

	queue< PtrT > currentVertices;
	currentVertices.push(&*it);

	unordered_set< PtrT > result = { &*it };

	while (!currentVertices.empty())
	{
		auto currentVertexPtr = currentVertices.front();
		currentVertices.pop();

		const CessorListType& successors = getSuccessorsFromPointer(currentVertexPtr);
		FORIT(vertexIt, successors)
		{
			auto successorPtr = *vertexIt;
			auto insertResult = result.insert(successorPtr);
			if (insertResult.second)
			{
				currentVertices.push(successorPtr); // non-seen yet vertex
			}
		}
	}

	return result;
}

template< class T, template < class X > class PtrTraitsT >
auto Graph< T, PtrTraitsT >::getReachableTo(const T& to) const -> unordered_set< PtrT >
{
	__successors.swap(__predecessors); // transposing the graph temporarily
	auto result = getReachableFrom(to);
	__successors.swap(__predecessors); // transposing it to original state
	return result;
}

}
}

#endif

