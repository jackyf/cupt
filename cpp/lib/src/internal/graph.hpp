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
#ifndef CUPT_INTERNAL_GRAPH_SEEN
#define CUPT_INTERNAL_GRAPH_SEEN

#include <algorithm>
#include <set>
#include <map>
#include <list>
#include <queue>

#include <cupt/common.hpp>

namespace cupt {
namespace internal {

using std::map;
using std::set;
using std::list;
using std::queue;

template < class T >
class Graph
{
	set< T > __vertices;
	map< const T*, list< const T* > > __predecessors;
	map< const T*, list< const T* > > __successors;

	static const list< const T* > __null_list;

 public:
	const set< T >& getVertices() const;
	vector< pair< const T*, const T* > > getEdges() const;

	bool hasEdge(const T& from, const T& to) const;

	const list< const T* >& getPredecessors(const T& vertex) const;
	const list< const T* >& getSuccessors(const T& vertex) const;
	const list< const T* >& getPredecessorsFromPointer(const T* vertexPtr) const;
	const list< const T* >& getSuccessorsFromPointer(const T* vertexPtr) const;


	const T* addVertex(const T& vertex);
	void deleteVertex(const T& vertex);

	void addEdge(const T& from, const T& to);
	void deleteEdge(const T& from, const T& to);

	set< T > getReachable(const T& from) const;
	vector< const T* > getPathVertices(const T& from, const T& to) const;

	vector< vector< T > > topologicalSortOfStronglyConnectedComponents();
};

template < class T >
const list< const T* > Graph< T >::__null_list;

template < class T >
const set< T >& Graph< T >::getVertices() const
{
	return __vertices;
}

template < class T >
vector< pair< const T*, const T* > > Graph< T >::getEdges() const
{
	vector< pair< const T*, const T* > > result;
	FORIT(vertexIt, __vertices)
	{
		const T* vertexPtr = &*vertexIt;

		const list< const T* >& predecessors = getPredecessorsFromPointer(vertexPtr);
		FORIT(predecessorPtrIt, predecessors)
		{
			result.push_back(std::make_pair(*predecessorPtrIt, vertexPtr));
		}
	}

	return result;
}

template < class T >
const T* Graph< T >::addVertex(const T& vertex)
{
	return &*(__vertices.insert(vertex).first);
}

template < class T >
void Graph< T >::deleteVertex(const T& vertex)
{
	// searching for vertex
	auto it = __vertices.find(vertex);
	if (it != __vertices.end())
	{
		auto vertexPtr = &*it;
		// deleting edges containing vertex
		const list< const T* >& predecessors = getPredecessorsFromPointer(vertexPtr);
		const list< const T* >& successors = getSuccessorsFromPointer(vertexPtr);
		FORIT(predecessorIt, predecessors)
		{
			__successors[*predecessorIt].remove(vertexPtr);
		}
		__predecessors.erase(vertexPtr);

		FORIT(successorIt, successors)
		{
			__predecessors[*successorIt].remove(vertexPtr);
		}
		__successors.erase(vertexPtr);

		// and the vertex itself
		__vertices.erase(it);
	}
}

template < class T >
bool Graph< T >::hasEdge(const T& from, const T& to) const
{
	const list< const T* >& predecessors = getPredecessors(to);
	FORIT(vertexIt, predecessors)
	{
		if (from == **vertexIt)
		{
			return true;
		}
	}
	return false;
}

template < class T >
void Graph< T >::addEdge(const T& from, const T& to)
{
	if (!hasEdge(from, to))
	{
		auto fromPtr = addVertex(from);
		auto toPtr = addVertex(to);
		__predecessors[toPtr].push_back(fromPtr);
		__successors[fromPtr].push_back(toPtr);
	}
}

template < class T >
void Graph< T >::deleteEdge(const T& from, const T& to)
{
	auto fromIt = __vertices.find(from);
	auto toIt = __vertices.find(to);
	if (fromIt == __vertices.end() || toIt == __vertices.end())
	{
		return;
	}
	auto fromPtr = &*fromIt;
	auto toPtr = &*toIt;

	auto predecessorsIt = __predecessors.find(toPtr);
	auto successorsIt = __successors.find(fromPtr);
	if (predecessorsIt != __predecessors.end())
	{
		predecessorsIt->second.remove(fromPtr);
	}
	if (successorsIt != __successors.end())
	{
		successorsIt->second.remove(toPtr);
	}
}

template < class T >
const list< const T* >& Graph< T >::getPredecessorsFromPointer(const T* vertexPtr) const
{
	auto it = __predecessors.find(vertexPtr);
	return (it != __predecessors.end() ? it->second : __null_list);
}

template < class T >
const list< const T* >& Graph< T >::getSuccessorsFromPointer(const T* vertexPtr) const
{
	auto it = __successors.find(vertexPtr);
	return (it != __successors.end() ? it->second : __null_list);
}

template < class T >
const list< const T* >& Graph< T >::getPredecessors(const T& vertex) const
{
	auto vertexIt = __vertices.find(vertex);
	if (vertexIt == __vertices.end())
	{
		return __null_list;
	}
	return getPredecessorsFromPointer(&*vertexIt);
}

template < class T >
const list< const T* >& Graph< T >::getSuccessors(const T& vertex) const
{
	auto vertexIt = __vertices.find(vertex);
	if (vertexIt == __vertices.end())
	{
		return __null_list;
	}
	return getSuccessorsFromPointer(&*vertexIt);
}

template < class T >
void __dfs_visit(const Graph< T >& graph, const T* vertexPtr,
		set< const T* >& seen, vector< const T* >& output)
{
	seen.insert(vertexPtr);

	const list< const T* >& successors = graph.getSuccessorsFromPointer(vertexPtr);
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
	// undefined order
	std::random_shuffle(vertices.begin(), vertices.end());

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
vector< vector< T > > __dfs_mode2(const Graph< T >& graph, const vector< const T* >& vertices)
{
	set< const T* > seen;
	vector< const T* > stronglyConnectedComponent;

	vector< vector< T > > result; // topologically sorted vertices

	FORIT(vertexIt, vertices)
	{
		if (!seen.count(*vertexIt))
		{
			__dfs_visit(graph, *vertexIt, seen, stronglyConnectedComponent);
			vector< T > toInsert;
			FORIT(vertexPtrIt, stronglyConnectedComponent)
			{
				toInsert.push_back(**vertexPtrIt);
			}
			result.push_back(std::move(toInsert));
			stronglyConnectedComponent.clear();
		}
	}

	return result;
}

template < class T >
vector< vector< T > > Graph< T >::topologicalSortOfStronglyConnectedComponents()
{
	auto vertices = __dfs_mode1(*this);

	// transposing the graph temporarily
	__successors.swap(__predecessors);

	std::reverse(vertices.begin(), vertices.end());

	auto result = __dfs_mode2(*this, vertices);

	// transposing it to original state
	__successors.swap(__predecessors);

	return result;
}

template < class T >
set< T > Graph< T >::getReachable(const T& from) const
{
	auto it = __vertices.find(from);
	if (it == __vertices.end())
	{
		return set< T >();
	}

	queue< const T* > currentVertices;
	currentVertices.push(&*it);

	set< T > result;

	while (!currentVertices.empty())
	{
		auto currentVertexPtr = currentVertices.front();
		currentVertices.pop();

		auto insertResult = result.insert(*currentVertexPtr);
		if (insertResult.second)
		{
			// new element was just inserted to seenVertices
			const list< const T* >& successors = getSuccessorsFromPointer(currentVertexPtr);
			FORIT(vertexIt, successors)
			{
				currentVertices.push(*vertexIt);
			}
		}
	}

	return result;
}

template < class T >
vector< const T* > Graph< T >::getPathVertices(const T& from, const T& to) const
{
	auto fromIt = __vertices.find(from);
	auto toIt = __vertices.find(to);

	if (fromIt == __vertices.end() || toIt == __vertices.end())
	{
		return vector< const T* >();
	}

	auto fromPtr = &*fromIt;
	auto toPtr = &*toIt;

	queue< pair< const T*, vector< const T* > > > verticesAndPaths;
    verticesAndPaths.push(make_pair(fromPtr, vector< const T* > { fromPtr }));

	set< const T* > seenVertices;

	while (!verticesAndPaths.empty())
	{
		auto element = verticesAndPaths.front();
		verticesAndPaths.pop();

		const T* currentVertexPtr = element.first;
		const vector< const T* >& currentPath = element.second;
		if (currentVertexPtr == toPtr)
		{
			return currentPath;
		}

		auto insertResult = seenVertices.insert(currentVertexPtr);
		if (insertResult.second)
		{
			// unseen element
			const list< const T* >& successors = getSuccessorsFromPointer(currentVertexPtr);
			FORIT(successorIt, successors)
			{
				vector< const T* > newPath = currentPath;
				newPath.push_back(*successorIt);
				verticesAndPaths.push(pair< const T*, vector< const T* > >(*successorIt, newPath));
			}
		}
	}

	// if not found
	return vector< const T* >();
}


}
}

#endif

