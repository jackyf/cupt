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
#include <unordered_map>
using std::unordered_map;
#include <list>
using std::list;

#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/sourcepackage.hpp>
#include <cupt/cache/sourceversion.hpp>

#include <internal/nativeresolver/solution.hpp>
#include <internal/nativeresolver/dependencygraph.hpp>

namespace cupt {
namespace internal {
namespace dependencygraph {

using cache::RelationExpression;
using std::make_pair;

InitialPackageEntry::InitialPackageEntry()
	: sticked(false), modified(false)
{}

size_t BasicVertex::getTypePriority() const
{
	fatal("internal error: getting priority of '%s'", toString().c_str());
	return 0; // unreachable
}

bool BasicVertex::isAnti() const
{
	fatal("internal error: getting isAnti of '%s'", toString().c_str());
	return false; // unreachable
}

shared_ptr< const Reason > BasicVertex::getReason(const BasicVertex&) const
{
	fatal("internal error: getting reason of '%s'", toString().c_str());
	return shared_ptr< const Reason >(); // unreachable
}

const forward_list< const Element* >* BasicVertex::getRelatedElements() const
{
	fatal("internal error: getting related elements of '%s'", toString().c_str());
	return NULL; // unreachable
}

Unsatisfied::Type BasicVertex::getUnsatisfiedType() const
{
	return Unsatisfied::None;
}

VersionVertex::VersionVertex(const map< string, forward_list< const Element* > >::iterator& it)
	: __related_element_ptrs_it(it)
{}

string VersionVertex::toString() const
{
	return getPackageName() + ' ' +
			(version ? version->versionString : "<not installed>");
}

const forward_list< const Element* >* VersionVertex::getRelatedElements() const
{
	return &__related_element_ptrs_it->second;
}

const string& VersionVertex::getPackageName() const
{
	return __related_element_ptrs_it->first;
}

string VersionVertex::toLocalizedString() const
{
	const string& packageName = getPackageName();
	if (version)
	{
		return __("installed ") + packageName + ' ' + version->versionString;
	}
	else
	{
		return __("removed ") + packageName;
	}
}

typedef BinaryVersion::RelationTypes::Type RelationType;

struct RelationExpressionVertex: public BasicVertex
{
	RelationType dependencyType;
	const RelationExpression* relationExpressionPtr;
	string specificPackageName;

	string toString() const;
	size_t getTypePriority() const;
	shared_ptr< const Reason > getReason(const BasicVertex& parent) const;
	bool isAnti() const;
	Unsatisfied::Type getUnsatisfiedType() const;
};

string RelationExpressionVertex::toString() const
{
	auto result = sf("%s '%s'", BinaryVersion::RelationTypes::rawStrings[dependencyType],
			relationExpressionPtr->toString().c_str());
	if (!specificPackageName.empty())
	{
		result += string(" [") + specificPackageName + ']';
	}
	return result;
}

size_t RelationExpressionVertex::getTypePriority() const
{
	switch (dependencyType)
	{
		case BinaryVersion::RelationTypes::Conflicts:
		case BinaryVersion::RelationTypes::Breaks:
		case BinaryVersion::RelationTypes::PreDepends:
		case BinaryVersion::RelationTypes::Depends:
			return 3;
		case BinaryVersion::RelationTypes::Recommends:
			return 2;
		case BinaryVersion::RelationTypes::Suggests:
			return 1;
		default:
			fatal("internal error: unsupported dependency type '%d'", int(dependencyType));
	}
	return 0; // unreacahble
}

bool RelationExpressionVertex::isAnti() const
{
	return dependencyType == BinaryVersion::RelationTypes::Conflicts ||
			dependencyType == BinaryVersion::RelationTypes::Breaks;
}

shared_ptr< const Reason > RelationExpressionVertex::getReason(const BasicVertex& parent) const
{
	typedef system::Resolver::RelationExpressionReason OurReason;

	auto versionParent = dynamic_cast< const VersionVertex* >(&parent);
	if (!versionParent)
	{
		fatal("internal error: a parent of relation expression vertex is not a version vertex");
	}
	return shared_ptr< const Reason >(
			new OurReason(versionParent->version,
			dependencyType, *relationExpressionPtr));
}

Unsatisfied::Type RelationExpressionVertex::getUnsatisfiedType() const
{
	switch (dependencyType)
	{
		case BinaryVersion::RelationTypes::Recommends:
			return Unsatisfied::Recommends;
		case BinaryVersion::RelationTypes::Suggests:
			return Unsatisfied::Suggests;
		default:
			return Unsatisfied::None;
	}
}

struct SynchronizeVertex: public BasicVertex
{
	string targetPackageName;
	bool isHard;

	SynchronizeVertex(bool isHard);
	string toString() const;
	size_t getTypePriority() const;
	shared_ptr< const Reason > getReason(const BasicVertex& parent) const;
	bool isAnti() const;
	Unsatisfied::Type getUnsatisfiedType() const;
};

SynchronizeVertex::SynchronizeVertex(bool isHard)
	: isHard(isHard)
{}

string SynchronizeVertex::toString() const
{
	return string("sync with ") + targetPackageName;
}

size_t SynchronizeVertex::getTypePriority() const
{
	return isHard ? 3 : 2;
}

shared_ptr< const Reason > SynchronizeVertex::getReason(const BasicVertex& parent) const
{
	auto versionParent = dynamic_cast< const VersionVertex* >(&parent);
	if (!versionParent)
	{
		fatal("internal error: a parent of relation expression vertex is not a version vertex");
	}
	return shared_ptr< const Reason >(
			new system::Resolver::SynchronizationReason(versionParent->version, targetPackageName));
}

bool SynchronizeVertex::isAnti() const
{
	return true;
}

Unsatisfied::Type SynchronizeVertex::getUnsatisfiedType() const
{
	return Unsatisfied::Sync;
}

struct UnsatisfiedVertex: public BasicVertex
{
	const Element* parent;

	string toString() const;
	const forward_list< const Element* >* getRelatedElements() const;
	Unsatisfied::Type getUnsatisfiedType() const;
};

string UnsatisfiedVertex::toString() const
{
	static const string u = "unsatisfied ";
	return u + (*parent)->toString();
}

const forward_list< const Element* >* UnsatisfiedVertex::getRelatedElements() const
{
	return NULL;
}

Unsatisfied::Type UnsatisfiedVertex::getUnsatisfiedType() const
{
	return (*parent)->getUnsatisfiedType();
}


bool __are_versions_equal(const shared_ptr< const BinaryVersion >& left,
		const shared_ptr< const BinaryVersion >& right)
{
	if (left)
	{
		return right && left->versionString == right->versionString;
	}
	else
	{
		return !right;
	}
}

bool __is_version_array_intersects_with_packages(
		const vector< shared_ptr< const BinaryVersion > >& versions,
		const map< string, shared_ptr< const BinaryVersion > >& oldPackages)
{
	FORIT(versionIt, versions)
	{
		const shared_ptr< const BinaryVersion >& version = *versionIt;
		const string& packageName = version->packageName;

		auto oldPackageIt = oldPackages.find(packageName);
		if (oldPackageIt == oldPackages.end())
		{
			continue;
		}

		if (version->versionString == oldPackageIt->second->versionString)
		{
			return true;
		}
	}

	return false;
}

bool __version_has_relation_expression(const shared_ptr< const BinaryVersion >& version,
		BinaryVersion::RelationTypes::Type dependencyType,
		const RelationExpression& relationExpression)
{
	auto relationExpressionString = relationExpression.getHashString();
	FORIT(candidateRelationExpressionIt, version->relations[dependencyType])
	{
		auto candidateString = candidateRelationExpressionIt->getHashString();
		if (relationExpressionString == candidateString)
		{
			return true;
		}
	}
	return false;
}

bool __is_soft_dependency_ignored(const Config& config,
		const shared_ptr< const BinaryVersion >& version,
		BinaryVersion::RelationTypes::Type dependencyType,
		const RelationExpression& relationExpression,
		const vector< shared_ptr< const BinaryVersion > >& satisfyingVersions,
		const map< string, shared_ptr< const BinaryVersion > >& oldPackages)
{
	auto wasSatisfiedInPast = __is_version_array_intersects_with_packages(
				satisfyingVersions, oldPackages);
	if (wasSatisfiedInPast)
	{
		return false;
	}

	if (dependencyType == BinaryVersion::RelationTypes::Recommends)
	{
		if (!config.getBool("apt::install-recommends"))
		{
			return true;
		}
	}
	else // Suggests
	{
		if (!config.getBool("apt::install-suggests"))
		{
			return true;
		}
	}

	auto oldPackageIt = oldPackages.find(version->packageName);
	if (oldPackageIt != oldPackages.end())
	{
		const shared_ptr< const BinaryVersion >& oldVersion = oldPackageIt->second;
		if (__version_has_relation_expression(oldVersion,
			dependencyType, relationExpression))
		{
			// the fact that we are here means that the old version of this package
			// had exactly the same relation expression, and it was unsatisfied
			// so, upgrading the version doesn't bring anything new
			return true;
		}
	}

	return false;
}

struct DependencyEntry
{
	RelationType type;
	bool isAnti;
};

vector< DependencyEntry > __get_dependency_groups(const Config& config)
{
	auto checkForUselessPairOption = [config](const string& subname)
	{
		auto installOptionName = string("apt::install-") + subname;
		if (config.getBool(installOptionName))
		{
			warn("a positive value of the option '%s' has no effect without a positive value of the option '%s'",
					installOptionName.c_str(),
					(string("cupt::resolver::keep-") + subname).c_str());
		}
	};

	vector< DependencyEntry > result = {
		{ BinaryVersion::RelationTypes::PreDepends, false },
		{ BinaryVersion::RelationTypes::Depends, false },
		{ BinaryVersion::RelationTypes::Conflicts, true },
		{ BinaryVersion::RelationTypes::Breaks, true },
	};

	if (config.getBool("cupt::resolver::keep-recommends"))
	{
		result.push_back(DependencyEntry { BinaryVersion::RelationTypes::Recommends, false });
	}
	else
	{
		checkForUselessPairOption("recommends");
	}

	if (config.getBool("cupt::resolver::keep-suggests"))
	{
		result.push_back(DependencyEntry { BinaryVersion::RelationTypes::Suggests, false });
	}
	else
	{
		checkForUselessPairOption("suggests");
	}

	return result;
}

DependencyGraph::DependencyGraph(const Config& config, const Cache& cache)
	: __config(config), __cache(cache)
{}

DependencyGraph::~DependencyGraph()
{
	const set< Element >& vertices = this->getVertices();
	FORIT(elementIt, vertices)
	{
		delete *elementIt;
	}
}

vector< string > __get_related_binary_package_names(const Cache& cache,
		const shared_ptr< const BinaryVersion >& version)
{
	const string& sourcePackageName = version->sourcePackageName;

	auto sourcePackage = cache.getSourcePackage(sourcePackageName);
	if (sourcePackage)
	{
		auto sourceVersion = static_pointer_cast< const SourceVersion >(
				sourcePackage->getSpecificVersion(version->sourceVersionString));
		if (sourceVersion)
		{
			// there will be at least one binary package name
			// ('<packageName>'), starting from this point
			return sourceVersion->binaryPackageNames;
		}
	}

	return vector< string >();
}

vector< shared_ptr< const BinaryVersion > > __get_versions_by_source_version_string(const Cache& cache,
		const string& packageName, const string& sourceVersionString)
{
	vector< shared_ptr< const BinaryVersion > > result;
	if (auto package = cache.getBinaryPackage(packageName))
	{
		auto versions = package->getVersions();
		FORIT(versionIt, versions)
		{
			if ((*versionIt)->sourceVersionString == sourceVersionString)
			{
				result.push_back(*versionIt);
			}
		}
	}

	return result;
}

short __get_synchronize_level(const Config& config)
{
	const string optionName = "cupt::resolver::synchronize-by-source-versions";
	auto optionValue = config.getString(optionName);
	if (optionValue == "none")
	{
		return 0;
	}
	else if (optionValue == "soft")
	{
		return 1;
	}
	else if (optionValue == "hard")
	{
		return 2;
	}
	fatal("the option '%s' can have only values 'none, 'soft' or 'hard'", optionName.c_str());
	return 0; // unreachable
}

class DependencyGraph::FillHelper
{
	DependencyGraph& __dependency_graph;
	const map< string, shared_ptr< const BinaryVersion > >& __old_packages;
	const map< string, InitialPackageEntry >& __initial_packages;
	queue< const Element* >& __to_process;
	bool __debugging;

	map< shared_ptr< const BinaryVersion >, const Element* > __version_to_vertex_ptr;
	unordered_map< string, const Element* > __relation_expression_to_vertex_ptr;
	unordered_map< string, list< pair< string, const Element* > > > __meta_anti_relation_expression_vertices;
	unordered_map< string, list< pair< string, const Element* > > > __meta_synchronize_map;

	bool __can_package_be_removed(const string& packageName) const
	{
		return !__dependency_graph.__config.getBool("cupt::resolver::no-remove") ||
				!__old_packages.count(packageName) ||
				__dependency_graph.__cache.isAutomaticallyInstalled(packageName);
	}

	// TODO: use VersionElement* when needed
 public:
	FillHelper(DependencyGraph& dependencyGraph,
			const map< string, shared_ptr< const BinaryVersion > >& oldPackages,
			const map< string, InitialPackageEntry >& initialPackages,
			queue< const Element* >& toProcess)
		: __dependency_graph(dependencyGraph),
		__old_packages(oldPackages), __initial_packages(initialPackages),
		__to_process(toProcess),
		__debugging(__dependency_graph.__config.getBool("debug::resolver"))
	{}

	const Element* getVertexPtr(const string& packageName, const shared_ptr< const BinaryVersion >& version)
	{
		auto isVertexAllowed = [this, &packageName, &version]() -> bool
		{
			auto initialPackageIt = this->__initial_packages.find(packageName);
			if (initialPackageIt != this->__initial_packages.end() && initialPackageIt->second.sticked)
			{
				if (!__are_versions_equal(version, initialPackageIt->second.version))
				{
					return false;
				}
			}

			if (!version && !__can_package_be_removed(packageName))
			{
				return false;
			}

			return true;
		};
		auto makeVertex = [this, &packageName, &version]() -> const Element*
		{
			static const forward_list< const Element* > nullElementList;
			auto relatedVertexPtrsIt = __dependency_graph.__package_name_to_vertex_ptrs.insert(
					make_pair(packageName, nullElementList)).first;
			auto vertex(new VersionVertex(relatedVertexPtrsIt));
			vertex->version = version;
			auto vertexPtr = __dependency_graph.addVertex(vertex);
			relatedVertexPtrsIt->second.push_front(vertexPtr);
			return vertexPtr;
		};

		const Element** elementPtrPtr;
		bool isNew;
		if (version)
		{
			auto insertResult = __version_to_vertex_ptr.insert(
					make_pair(version, (const Element*)NULL));
			isNew = insertResult.second;
			elementPtrPtr = &insertResult.first->second;
		}
		else
		{
			auto insertResult = __dependency_graph.__empty_package_to_vertex_ptr.insert(
					make_pair(packageName, (const Element*)NULL));
			isNew = insertResult.second;
			elementPtrPtr = &insertResult.first->second;
		}

		if (isNew && isVertexAllowed())
		{
			// needs new vertex
			*elementPtrPtr = makeVertex();
		}
		return *elementPtrPtr;
	}

 private:
	void addEdgeCustom(const Element* fromVertexPtr, const Element* toVertexPtr)
	{
		__dependency_graph.addEdgeFromPointers(fromVertexPtr, toVertexPtr);
		if (__debugging)
		{
			debug("adding an edge '%s' -> '%s'",
					(*fromVertexPtr)->toString().c_str(), (*toVertexPtr)->toString().c_str());
		}
	}

	const Element* getVertexPtrForRelationExpression(const RelationExpression* relationExpressionPtr,
			const RelationType& dependencyType, bool* isNew)
	{
		auto hashKey = relationExpressionPtr->getHashString() + char('0' + dependencyType);
		const Element*& elementPtr = __relation_expression_to_vertex_ptr.insert(
				make_pair(hashKey, (const Element*)NULL)).first->second;
		*isNew = !elementPtr;
		if (!elementPtr)
		{
			auto vertex(new RelationExpressionVertex);
			vertex->dependencyType = dependencyType;
			vertex->relationExpressionPtr = relationExpressionPtr;
			elementPtr = __dependency_graph.addVertex(vertex);
		}
		return elementPtr;
	}

 public:
	const Element* getVertexPtrForEmptyPackage(const string& packageName)
	{
		return getVertexPtr(packageName, shared_ptr< const BinaryVersion >());
	}

	const Element* queueVersion(const shared_ptr< const BinaryVersion >& version)
	{
		const set< Element >& vertices = __dependency_graph.getVertices();
		auto oldSize = vertices.size();
		auto elementPtr = getVertexPtr(version->packageName, version);
		if (vertices.size() > oldSize && elementPtr) // newly inserted
		{
			__to_process.push(elementPtr);
		}
		return elementPtr;
	}

	void processAntiRelation(const string& packageName,
			const Element* vertexPtr, const RelationExpression& relationExpression,
			BinaryVersion::RelationTypes::Type dependencyType)
	{
		auto hashKey = relationExpression.getHashString() + char('0' + dependencyType);
		static const list< pair< string, const Element* > > emptyList;
		auto insertResult = __meta_anti_relation_expression_vertices.insert(
				make_pair(hashKey, emptyList));
		bool isNewRelationExpressionVertex = insertResult.second;
		list< pair< string, const Element* > >& subElementPtrs = insertResult.first->second;

		if (isNewRelationExpressionVertex)
		{
			auto satisfyingVersions = __dependency_graph.__cache.getSatisfyingVersions(relationExpression);
			// filling sub elements
			map< string, list< const BinaryVersion* > > groupedSatisfiedVersions;
			FORIT(satisfyingVersionIt, satisfyingVersions)
			{
				groupedSatisfiedVersions[(*satisfyingVersionIt)->packageName].push_back(
						satisfyingVersionIt->get());
			}

			FORIT(groupIt, groupedSatisfiedVersions)
			{
				const string& packageName = groupIt->first;

				auto subVertex(new RelationExpressionVertex);
				subVertex->dependencyType = dependencyType;
				subVertex->relationExpressionPtr = &relationExpression;
				subVertex->specificPackageName = packageName;
				auto subVertexPtr = __dependency_graph.addVertex(subVertex);
				subElementPtrs.push_back(make_pair(packageName, subVertexPtr));

				auto package = __dependency_graph.__cache.getBinaryPackage(packageName);
				if (!package)
				{
					fatal("internal error: the binary package '%s' doesn't exist", packageName.c_str());
				}
				auto packageVersions = package->getVersions();
				const list< const BinaryVersion* >& subSatisfiedVersions = groupIt->second;
				FORIT(packageVersionIt, packageVersions)
				{
					auto predicate = [&packageVersionIt](const BinaryVersion* left) -> bool
					{
						return *left == **packageVersionIt;
					};
					if (std::find_if(subSatisfiedVersions.begin(), subSatisfiedVersions.end(),
								predicate) == subSatisfiedVersions.end())
					{
						if (auto queuedVersionPtr = queueVersion(*packageVersionIt))
						{
							addEdgeCustom(subVertexPtr, queuedVersionPtr);
						}
					}
				}

				if (auto emptyPackageElementPtr = getVertexPtrForEmptyPackage(packageName))
				{
					addEdgeCustom(subVertexPtr, emptyPackageElementPtr);
				}
			}
		}
		FORIT(subElementPtrIt, subElementPtrs)
		{
			if (subElementPtrIt->first == packageName)
			{
				continue; // doesn't conflict with itself
			}
			addEdgeCustom(vertexPtr, subElementPtrIt->second);
		}
	}

	void processForwardRelation(const shared_ptr< const BinaryVersion >& version,
			const Element* vertexPtr, const RelationExpression& relationExpression,
			BinaryVersion::RelationTypes::Type dependencyType)
	{
		vector< shared_ptr< const BinaryVersion > > satisfyingVersions;
		// very expensive, delay calculation as possible
		bool calculatedSatisfyingVersions = false;

		if (dependencyType == BinaryVersion::RelationTypes::Recommends ||
				dependencyType == BinaryVersion::RelationTypes::Suggests)
		{
			satisfyingVersions = __dependency_graph.__cache.getSatisfyingVersions(relationExpression);
			if (__is_soft_dependency_ignored(__dependency_graph.__config, version, dependencyType,
					relationExpression, satisfyingVersions, __old_packages))
			{
				if (__debugging)
				{
					debug("ignoring soft dependency relation: %s: %s '%s'",
							(*vertexPtr)->toString().c_str(),
							BinaryVersion::RelationTypes::rawStrings[dependencyType],
							relationExpression.toString().c_str());
				}
				return;
			}
			calculatedSatisfyingVersions = true;
		}

		bool isNewRelationExpressionVertex;
		auto relationExpressionVertexPtr = getVertexPtrForRelationExpression(
				&relationExpression, dependencyType, &isNewRelationExpressionVertex);
		addEdgeCustom(vertexPtr, relationExpressionVertexPtr);
		if (!isNewRelationExpressionVertex)
		{
			return;
		}

		if (!calculatedSatisfyingVersions)
		{
			satisfyingVersions = __dependency_graph.__cache.getSatisfyingVersions(relationExpression);
		}

		FORIT(satisfyingVersionIt, satisfyingVersions)
		{
			if (auto queuedVersionPtr = queueVersion(*satisfyingVersionIt))
			{
				addEdgeCustom(relationExpressionVertexPtr, queuedVersionPtr);
			}
		}

		if (dependencyType == BinaryVersion::RelationTypes::Recommends ||
				dependencyType == BinaryVersion::RelationTypes::Suggests)
		{
			auto notSatisfiedVertex(new UnsatisfiedVertex);
			notSatisfiedVertex->parent = relationExpressionVertexPtr;
			addEdgeCustom(relationExpressionVertexPtr, __dependency_graph.addVertex(notSatisfiedVertex));
		}
	}

	void processSynchronizations(const shared_ptr< const BinaryVersion >& version,
			const Element* vertexPtr, short level)
	{
		auto hashKey = version->sourcePackageName + ' ' + version->sourceVersionString;
		static const list< pair< string, const Element* > > emptyList;
		auto insertResult = __meta_synchronize_map.insert(make_pair(hashKey, emptyList));
		bool isNewMetaVertex = insertResult.second;
		list< pair< string, const Element* > >& subElementPtrs = insertResult.first->second;

		if (isNewMetaVertex)
		{
			auto packageNames = __get_related_binary_package_names(__dependency_graph.__cache, version);
			FORIT(packageNameIt, packageNames)
			{
				auto syncVertex = new SynchronizeVertex(level > 1);
				syncVertex->targetPackageName = *packageNameIt;
				auto syncVertexPtr = __dependency_graph.addVertex(syncVertex);

				auto relatedVersions = __get_versions_by_source_version_string(
						__dependency_graph.__cache, *packageNameIt, version->sourceVersionString);
				FORIT(relatedVersionIt, relatedVersions)
				{
					if (auto relatedVersionVertexPtr = queueVersion(*relatedVersionIt))
					{
						addEdgeCustom(syncVertexPtr, relatedVersionVertexPtr);
					}
				}

				if (auto emptyVersionPtr = getVertexPtrForEmptyPackage(*packageNameIt))
				{
					addEdgeCustom(syncVertexPtr, emptyVersionPtr);
				}

				if (level == 1) // soft
				{
					auto unsatisfiedVertex = new UnsatisfiedVertex;
					unsatisfiedVertex->parent = syncVertexPtr;
					addEdgeCustom(syncVertexPtr, __dependency_graph.addVertex(unsatisfiedVertex));
				}

				subElementPtrs.push_back(make_pair(*packageNameIt, syncVertexPtr));
			}
		}

		FORIT(subElementPtrIt, subElementPtrs)
		{
			if (subElementPtrIt->first == version->packageName)
			{
				continue; // don't synchronize with itself
			}
			addEdgeCustom(vertexPtr, subElementPtrIt->second);
		}
	}
};

vector< pair< const dg::Element*, PackageEntry > > DependencyGraph::fill(
		const map< string, shared_ptr< const BinaryVersion > >& oldPackages,
		const map< string, InitialPackageEntry >& initialPackages)
{
	queue< const Element* > toProcess;
	DependencyGraph::FillHelper helper(*this, oldPackages, initialPackages, toProcess);

	{ // getting elements from initial packages
		FORIT(it, initialPackages)
		{
			const InitialPackageEntry& initialPackageEntry = it->second;
			const shared_ptr< const BinaryVersion >& initialVersion = initialPackageEntry.version;

			if (initialVersion)
			{
				helper.queueVersion(initialVersion);

				if (!initialPackageEntry.sticked)
				{
					const string& packageName = it->first;
					auto package = __cache.getBinaryPackage(packageName);
					auto versions = package->getVersions();
					FORIT(versionIt, versions)
					{
						helper.queueVersion(*versionIt);
					}

					helper.getVertexPtrForEmptyPackage(packageName); // also, empty one
				}
			}
		}
	}

	auto synchronizeLevel = __get_synchronize_level(__config);
	auto dependencyGroups = __get_dependency_groups(__config);
	while (!toProcess.empty())
	{
		auto vertexPtr = toProcess.front();
		toProcess.pop();
		// persistent one
		auto version = static_cast< const VersionVertex* >(*vertexPtr)->version;

		FORIT(dependencyGroupIt, dependencyGroups)
		{
			auto dependencyType = dependencyGroupIt->type;
			auto isDependencyAnti = dependencyGroupIt->isAnti;

			const RelationLine& relationLine = version->relations[dependencyType];
			FORIT(relationExpressionIt, relationLine)
			{
				const RelationExpression& relationExpression = *relationExpressionIt;

				if (isDependencyAnti)
				{
					helper.processAntiRelation(version->packageName, vertexPtr, relationExpression, dependencyType);
				}
				else
				{
					helper.processForwardRelation(version, vertexPtr, relationExpression, dependencyType);
				}
			}
		}

		if (synchronizeLevel && !version->isInstalled())
		{
			helper.processSynchronizations(version, vertexPtr, synchronizeLevel);
		}
	}

	vector< pair< const Element*, PackageEntry > > result;
	{ // generating solution elements
		FORIT(it, initialPackages)
		{
			auto elementPtr = helper.getVertexPtr(it->first, it->second.version);
			PackageEntry packageEntry;
			packageEntry.sticked = it->second.sticked;
			result.push_back(make_pair(elementPtr, packageEntry));
		}
		PackageEntry defaultPackageEntry;
		FORIT(it, __empty_package_to_vertex_ptr)
		{
			auto emptyElementPtr = it->second;
			if (!initialPackages.count(it->first))
			{
				result.push_back(make_pair(emptyElementPtr, defaultPackageEntry));
			}
		}
	}
	return result;
}

const Element* DependencyGraph::getCorrespondingEmptyElement(const Element* elementPtr)
{
	auto versionVertex = dynamic_cast< const VersionVertex* >(*elementPtr);
	if (!versionVertex)
	{
		fatal("internal error: getting corresponding empty element for non-version vertex");
	}
	const string& packageName = versionVertex->getPackageName();
	auto it = __empty_package_to_vertex_ptr.find(packageName);
	if (it == __empty_package_to_vertex_ptr.end())
	{
		// it's an unreachable empty element, but we need some container for it
		auto vertex(new VersionVertex(__package_name_to_vertex_ptrs.find(packageName)));
		auto vertexPtr = this->addVertex(vertex);

		const Element*& elementPtr = __empty_package_to_vertex_ptr[packageName];
		elementPtr = vertexPtr;
		return elementPtr;
	}
	else
	{
		return it->second;
	}
}

}
}
}

