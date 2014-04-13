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
#include <cupt/versionstring.hpp>

#include <internal/nativeresolver/solution.hpp>
#include <internal/nativeresolver/dependencygraph.hpp>

namespace cupt {
namespace internal {
namespace dependencygraph {

using cache::RelationExpression;
using std::make_pair;

InitialPackageEntry::InitialPackageEntry()
	: version(NULL), modified(false)
{}

BasicVertex::~BasicVertex()
{}

size_t BasicVertex::getTypePriority() const
{
	fatal2i("getting priority of '%s'", toString());
	return 0; // unreachable
}

uint32_t BasicVertex::__next_id = 0;

BasicVertex::BasicVertex()
	: id(__next_id++)
{}

bool BasicVertex::isAnti() const
{
	fatal2i("getting isAnti of '%s'", toString());
	return false; // unreachable
}

shared_ptr< const Reason > BasicVertex::getReason(const BasicVertex&) const
{
	fatal2i("getting reason of '%s'", toString());
	return shared_ptr< const Reason >(); // unreachable
}

const forward_list< const Element* >* BasicVertex::getRelatedElements() const
{
	fatal2i("getting related elements of '%s'", toString());
	return NULL; // unreachable
}

Unsatisfied::Type BasicVertex::getUnsatisfiedType() const
{
	return Unsatisfied::None;
}

const RequestImportance& BasicVertex::getUnsatisfiedImportance() const
{
	fatal2i("getting unsatisfied importance of '%s'", toString());
	return *(const RequestImportance*)nullptr; // unreachable
}

bool BasicVertex::asAuto() const
{
	fatal2i("getting asAuto of '%s'", toString());
	return true; // unreacahble
}

const Element* BasicVertex::getFamilyKey() const
{
	return this;
}

VersionVertex::VersionVertex(const FamilyMap::iterator& it)
	: __related_element_ptrs_it(it)
{}

string VersionVertex::toString() const
{
	return getPackageName() + ' ' +
			(version ? version->versionString : "<not installed>");
}

const forward_list< const Element* >* VersionVertex::getRelatedElements() const
{
	return &__related_element_ptrs_it->second.first;
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
		return string(__("installed")) + ' ' + packageName + ' ' + version->versionString;
	}
	else
	{
		return string(__("removed")) + ' ' + packageName;
	}
}

const Element* VersionVertex::getFamilyKey() const
{
	return __related_element_ptrs_it->second.second;
}

typedef BinaryVersion::RelationTypes::Type RelationType;

struct RelationExpressionVertex: public BasicVertex
{
	RelationType dependencyType;
	const RelationExpression* relationExpressionPtr;

	string toString() const;
	size_t getTypePriority() const;
	shared_ptr< const Reason > getReason(const BasicVertex& parent) const;
	bool isAnti() const;
	Unsatisfied::Type getUnsatisfiedType() const;
	bool asAuto() const
	{
		return true;
	}
};

string RelationExpressionVertex::toString() const
{
	return format2("%s '%s'", BinaryVersion::RelationTypes::rawStrings[dependencyType],
			relationExpressionPtr->toString());
}

size_t RelationExpressionVertex::getTypePriority() const
{
	switch (dependencyType)
	{
		case BinaryVersion::RelationTypes::PreDepends:
		case BinaryVersion::RelationTypes::Depends:
			return 3;
		case BinaryVersion::RelationTypes::Recommends:
			return 2;
		case BinaryVersion::RelationTypes::Suggests:
			return 1;
		default:
			fatal2i("unsupported dependency type '%d'", int(dependencyType));
	}
	return 0; // unreacahble
}

bool RelationExpressionVertex::isAnti() const
{
	return false;
}

shared_ptr< const Reason > RelationExpressionVertex::getReason(const BasicVertex& parent) const
{
	typedef system::Resolver::RelationExpressionReason OurReason;

	auto versionParent = dynamic_cast< const VersionVertex* >(&parent);
	if (!versionParent)
	{
		fatal2i("a parent of relation expression vertex is not a version vertex");
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

struct AntiRelationExpressionVertex: public RelationExpressionVertex
{
	string specificPackageName;

	string toString() const
	{
		return RelationExpressionVertex::toString() + " [" + specificPackageName + ']';
	}
	size_t getTypePriority() const
	{
		return 3;
	}
	bool isAnti() const
	{
		return true;
	}
	Unsatisfied::Type getUnsatisfiedType() const
	{
		return Unsatisfied::None;
	}
};

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
		fatal2i("a parent of synchronize vertex is not a version vertex");
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
	return u + parent->toString();
}

const forward_list< const Element* >* UnsatisfiedVertex::getRelatedElements() const
{
	return NULL;
}

Unsatisfied::Type UnsatisfiedVertex::getUnsatisfiedType() const
{
	return parent->getUnsatisfiedType();
}

class CustomUnsatisfiedVertex: public UnsatisfiedVertex
{
	RequestImportance importance;
 public:
	CustomUnsatisfiedVertex(const RequestImportance& importance_)
		: importance(importance_)
	{}
	Unsatisfied::Type getUnsatisfiedType() const
	{
		return Unsatisfied::Custom;
	}
	const RequestImportance& getUnsatisfiedImportance() const
	{
		return importance;
	}
};

class AnnotatedUserReason: public system::Resolver::UserReason
{
	string p_annotation;
 public:
	AnnotatedUserReason(const string& annotation)
		: p_annotation(annotation)
	{}
	string toString() const
	{
		return UserReason::toString() + ": " + p_annotation;
	}
};

struct UserRelationExpressionVertex: public BasicVertex
{
	bool invert;
	bool asAutoFlag;
	string annotation;

	UserRelationExpressionVertex(const UserRelationExpression& ure)
		: invert(ure.invert)
		, asAutoFlag(ure.asAuto)
		, annotation(ure.annotation)
	{}
	size_t getTypePriority() const
	{
		return 4;
	}
	bool isAnti() const
	{
		return invert;
	}
	bool asAuto() const
	{
		return asAutoFlag;
	}
	shared_ptr< const Reason > getReason(const BasicVertex&) const
	{
		return std::make_shared< const AnnotatedUserReason >(annotation);
	}
	string toString() const
	{
		return "custom: " + annotation;
	}
};

bool __is_version_array_intersects_with_packages(
		const vector< const BinaryVersion* >& versions,
		const map< string, const BinaryVersion* >& oldPackages)
{
	for (const auto& version: versions)
	{
		auto oldPackageIt = oldPackages.find(version->packageName);
		if (oldPackageIt == oldPackages.end())
		{
			continue;
		}

		if (version == oldPackageIt->second)
		{
			return true;
		}
	}

	return false;
}

bool __version_has_relation_expression(const BinaryVersion* version,
		BinaryVersion::RelationTypes::Type dependencyType,
		const RelationExpression& relationExpression)
{
	auto relationExpressionString = relationExpression.getHashString();
	for (const RelationExpression& candidateRelationExpression: version->relations[dependencyType])
	{
		auto candidateString = candidateRelationExpression.getHashString();
		if (relationExpressionString == candidateString)
		{
			return true;
		}
	}
	return false;
}

bool __is_soft_dependency_ignored(const Config& config,
		const BinaryVersion* version,
		BinaryVersion::RelationTypes::Type dependencyType,
		const RelationExpression& relationExpression,
		const vector< const BinaryVersion* >& satisfyingVersions,
		const map< string, const BinaryVersion* >& oldPackages)
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
		auto& oldVersion = oldPackageIt->second;
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
			warn2(__("a positive value of the option '%s' has no effect without a positive value of the option '%s'"),
					installOptionName, string("cupt::resolver::keep-") + subname);
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
	const set< const Element* >& vertices = this->getVertices();
	FORIT(elementIt, vertices)
	{
		delete *elementIt;
	}
}

vector< string > __get_related_binary_package_names(const Cache& cache, const BinaryVersion* version)
{
	const string& sourcePackageName = version->sourcePackageName;

	auto sourcePackage = cache.getSourcePackage(sourcePackageName);
	if (sourcePackage)
	{
		auto sourceVersion = static_cast< const SourceVersion* >(
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

vector< const BinaryVersion* > __get_versions_by_source_version_string(const Cache& cache,
		const string& packageName, const string& sourceVersionString)
{
	vector< const BinaryVersion* > result;
	if (auto package = cache.getBinaryPackage(packageName))
	{
		for (auto version: *package)
		{
			if (version->sourceVersionString == sourceVersionString)
			{
				result.push_back(version);
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
	fatal2(__("the option '%s' can have only values 'none', 'soft' or 'hard'"), optionName);
	return 0; // unreachable
}

class DependencyGraph::FillHelper
{
	DependencyGraph& __dependency_graph;
	const map< string, const BinaryVersion* >& __old_packages;
	bool __debugging;

	int __synchronize_level;
	vector< DependencyEntry > __dependency_groups;

	map< string, pair< forward_list< const Element* >, const Element* > > __package_name_to_vertex_ptrs;
	unordered_map< string, const VersionVertex* > __version_to_vertex_ptr;
	unordered_map< string, const Element* > __relation_expression_to_vertex_ptr;
	unordered_map< string, map< string, const Element* > > __meta_anti_relation_expression_vertices;
	unordered_map< string, list< pair< string, const Element* > > > __meta_synchronize_map;
	const Element* p_dummyElementPtr;

	set< const Element* > __unfolded_elements;

	bool __can_package_be_removed(const string& packageName) const
	{
		return !__dependency_graph.__config.getBool("cupt::resolver::no-remove") ||
				!__old_packages.count(packageName) ||
				__dependency_graph.__cache.isAutomaticallyInstalled(packageName);
	}

 public:
	FillHelper(DependencyGraph& dependencyGraph,
			const map< string, const BinaryVersion* >& oldPackages)
		: __dependency_graph(dependencyGraph)
		, __old_packages(oldPackages)
		, __debugging(__dependency_graph.__config.getBool("debug::resolver"))
	{
		__synchronize_level = __get_synchronize_level(__dependency_graph.__config);
		__dependency_groups= __get_dependency_groups(__dependency_graph.__config);
		p_dummyElementPtr = getVertexPtrForEmptyPackage("<user requests>");
	}

	const VersionVertex* getVertexPtr(const string& packageName, const BinaryVersion* version, bool overrideChecks = false)
	{
		auto isVertexAllowed = [this, &packageName, &version]() -> bool
		{
			if (!version && !__can_package_be_removed(packageName))
			{
				return false;
			}

			if (version)
			{
				for (const BasicVertex* bv: __package_name_to_vertex_ptrs[packageName].first)
				{
					auto existingVersion = (static_cast< const VersionVertex* >(bv))->version;
					if (!existingVersion) continue;
					if (versionstring::sameOriginal(version->versionString, existingVersion->versionString))
					{
						if (std::equal(version->relations,
									version->relations + BinaryVersion::RelationTypes::Count,
									existingVersion->relations))
						{
							return false; // no reasons to allow this version dependency-wise
						}
					}
				}
			}

			return true;
		};
		auto makeVertex = [this, &packageName, &version]() -> const VersionVertex*
		{
			auto relatedVertexPtrsIt = __package_name_to_vertex_ptrs.insert(
					{ packageName, { {}, nullptr } }).first;
			auto vertexPtr(new VersionVertex(relatedVertexPtrsIt));
			vertexPtr->version = version;
			__dependency_graph.addVertex(vertexPtr);
			relatedVertexPtrsIt->second.first.push_front(vertexPtr);
			if (!relatedVertexPtrsIt->second.second)
			{
				relatedVertexPtrsIt->second.second = vertexPtr; // family vertex
			}
			return vertexPtr;
		};

		string versionHashString = packageName;
		versionHashString.append((const char*)&version, sizeof(version));
		auto insertResult = __version_to_vertex_ptr.insert({ std::move(versionHashString), nullptr });
		bool isNew = insertResult.second;
		const VersionVertex** elementPtrPtr = &insertResult.first->second;

		if ((isNew && isVertexAllowed()) || (overrideChecks && !*elementPtrPtr))
		{
			// needs new vertex
			*elementPtrPtr = makeVertex();
		}
		return *elementPtrPtr;
	}

	const VersionElement* getVertexPtr(const BinaryVersion* version, bool overrideChecks = false)
	{
		return getVertexPtr(version->packageName, version, overrideChecks);
	}

 private:
	void addEdgeCustom(const Element* fromVertexPtr, const Element* toVertexPtr)
	{
		__dependency_graph.addEdge(fromVertexPtr, toVertexPtr);
	}

	const Element* getVertexPtrForRelationExpression(const RelationExpression* relationExpressionPtr,
			const RelationType& dependencyType, bool* isNew)
	{
		auto hashKey = relationExpressionPtr->getHashString() + char('0' + dependencyType);
		const Element*& elementPtr = __relation_expression_to_vertex_ptr.insert(
				make_pair(std::move(hashKey), (const Element*)NULL)).first->second;
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
	const Element* getVertexPtrForEmptyPackage(const string& packageName, bool overrideChecks = false)
	{
		return getVertexPtr(packageName, nullptr, overrideChecks);
	}

 private:
	void buildEdgesForAntiRelationExpression(
			map< string, const Element* >* packageNameToSubElements,
			const vector< const BinaryVersion* > satisfyingVersions,
			const std::function< const Element* (const string&) >& createVertex,
			bool overrideChecks = false)
	{
		for (auto satisfyingVersion: satisfyingVersions)
		{
			const string& packageName = satisfyingVersion->packageName;
			auto& subElement = (*packageNameToSubElements)[packageName];
			if (subElement) continue;

			subElement = createVertex(packageName);

			auto package = __dependency_graph.__cache.getBinaryPackage(packageName);
			if (!package) fatal2i("the binary package '%s' doesn't exist", packageName);

			for (auto packageVersion: *package)
			{
				if (std::find(satisfyingVersions.begin(), satisfyingVersions.end(),
							packageVersion) == satisfyingVersions.end())
				{
					if (auto queuedVersionPtr = getVertexPtr(packageVersion, overrideChecks))
					{
						addEdgeCustom(subElement, queuedVersionPtr);
					}
				}
			}

			if (auto emptyPackageElementPtr = getVertexPtrForEmptyPackage(packageName, overrideChecks))
			{
				addEdgeCustom(subElement, emptyPackageElementPtr);
			}
		}
	}
	void processAntiRelation(const string& packageName,
			const Element* vertexPtr, const RelationExpression& relationExpression,
			BinaryVersion::RelationTypes::Type dependencyType)
	{
		auto hashKey = relationExpression.getHashString() + char('0' + dependencyType);
		static const map< string, const Element* > emptyMap;
		auto insertResult = __meta_anti_relation_expression_vertices.insert(
				make_pair(std::move(hashKey), emptyMap));
		bool isNewRelationExpressionVertex = insertResult.second;
		auto& packageNameToSubElements = insertResult.first->second;

		if (isNewRelationExpressionVertex)
		{
			auto createVertex = [&](const string& packageName)
			{
				auto subVertex(new AntiRelationExpressionVertex);
				subVertex->dependencyType = dependencyType;
				subVertex->relationExpressionPtr = &relationExpression;
				subVertex->specificPackageName = packageName;
				return __dependency_graph.addVertex(subVertex);
			};
			auto satisfyingVersions = __dependency_graph.__cache.getSatisfyingVersions(relationExpression);
			buildEdgesForAntiRelationExpression(&packageNameToSubElements, satisfyingVersions, createVertex);
		}
		for (const auto& it: packageNameToSubElements)
		{
			if (it.first == packageName)
			{
				continue; // doesn't conflict with itself
			}
			addEdgeCustom(vertexPtr, it.second);
		}
	}

	void processForwardRelation(const BinaryVersion* version,
			const Element* vertexPtr, const RelationExpression& relationExpression,
			BinaryVersion::RelationTypes::Type dependencyType)
	{
		vector< const BinaryVersion* > satisfyingVersions;
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
					debug2("ignoring soft dependency relation: %s: %s '%s'",
							vertexPtr->toString(),
							BinaryVersion::RelationTypes::rawStrings[dependencyType],
							relationExpression.toString());
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
			if (auto queuedVersionPtr = getVertexPtr(*satisfyingVersionIt))
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

	void processSynchronizations(const BinaryVersion*& version, const Element* vertexPtr)
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
				auto syncVertex = new SynchronizeVertex(__synchronize_level > 1);
				syncVertex->targetPackageName = *packageNameIt;
				auto syncVertexPtr = __dependency_graph.addVertex(syncVertex);

				auto relatedVersions = __get_versions_by_source_version_string(
						__dependency_graph.__cache, *packageNameIt, version->sourceVersionString);
				FORIT(relatedVersionIt, relatedVersions)
				{
					if (auto relatedVersionVertexPtr = getVertexPtr(*relatedVersionIt))
					{
						addEdgeCustom(syncVertexPtr, relatedVersionVertexPtr);
					}
				}

				if (auto emptyVersionPtr = getVertexPtrForEmptyPackage(*packageNameIt))
				{
					addEdgeCustom(syncVertexPtr, emptyVersionPtr);
				}

				if (__synchronize_level == 1) // soft
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

	const Element* createCustomUnsatisfiedElement(const Element* parent, const RequestImportance& importance)
	{
		auto vertex = new CustomUnsatisfiedVertex(importance);
		vertex->parent = parent;
		return __dependency_graph.addVertex(vertex);
	}

 public:
	void unfoldElement(const Element* elementPtr)
	{
		if (!__unfolded_elements.insert(elementPtr).second)
		{
			return; // processed already
		}
		auto versionElementPtr = dynamic_cast< const VersionElement* >(elementPtr);
		if (!versionElementPtr)
		{
			return; // nothing to process
		}

		// persistent one
		auto version = versionElementPtr->version;
		if (!version)
		{
			return;
		}

		for (const auto& dependencyGroup: __dependency_groups)
		{
			auto dependencyType = dependencyGroup.type;
			auto isDependencyAnti = dependencyGroup.isAnti;

			for (const auto& relationExpression: version->relations[dependencyType])
			{
				if (isDependencyAnti)
				{
					processAntiRelation(version->packageName, elementPtr, relationExpression, dependencyType);
				}
				else
				{
					processForwardRelation(version, elementPtr, relationExpression, dependencyType);
				}
			}
		}

		if (__synchronize_level && !version->isInstalled())
		{
			processSynchronizations(version, elementPtr);
		}
	}

	const Element* getDummyElementPtr() const
	{
		return p_dummyElementPtr;
	}

	void addUserRelationExpression(const UserRelationExpression& ure)
	{
		const Element* unsatisfiedElement = nullptr;

		auto createVertex = [&](const string&) -> const Element*
		{
			auto vertex = new UserRelationExpressionVertex(ure);
			__dependency_graph.addVertex(vertex);
			addEdgeCustom(p_dummyElementPtr, vertex);
			if (ure.importance != RequestImportance::Must)
			{
				if (!unsatisfiedElement) unsatisfiedElement = createCustomUnsatisfiedElement(vertex, ure.importance);
				addEdgeCustom(vertex, unsatisfiedElement);
			}
			return vertex;
		};

		auto satisfyingVersions = __dependency_graph.__cache.getSatisfyingVersions(ure.expression);
		if (!ure.invert)
		{
			static string dummy;
			auto vertex = createVertex(dummy);
			for (auto version: satisfyingVersions)
			{
				addEdgeCustom(vertex, getVertexPtr(version, true));
			}
		}
		else
		{
			map< string, const Element* > subElements;
			buildEdgesForAntiRelationExpression(&subElements, satisfyingVersions, createVertex, true);
		}
	}
};

vector< pair< const dg::Element*, shared_ptr< const PackageEntry > > > DependencyGraph::fill(
		const map< string, const BinaryVersion* >& oldPackages,
		const map< string, InitialPackageEntry >& initialPackages)
{
	__fill_helper.reset(new DependencyGraph::FillHelper(*this, oldPackages));

	{ // getting elements from initial packages
		FORIT(it, initialPackages)
		{
			const InitialPackageEntry& initialPackageEntry = it->second;
			const auto& initialVersion = initialPackageEntry.version;

			if (initialVersion)
			{
				__fill_helper->getVertexPtr(initialVersion);

				const string& packageName = it->first;
				auto package = __cache.getBinaryPackage(packageName);
				for (auto version: *package)
				{
					__fill_helper->getVertexPtr(version);
				}

				__fill_helper->getVertexPtrForEmptyPackage(packageName); // also, empty one
			}
		}
	}

	return p_generateSolutionElements(initialPackages);
}

vector< pair< const dg::Element*, shared_ptr< const PackageEntry > > > DependencyGraph::p_generateSolutionElements(
		const map< string, InitialPackageEntry >& initialPackages)
{
	vector< pair< const Element*, shared_ptr< const PackageEntry > > > result;

	auto generate = [&result](const Element* element, bool sticked)
	{
		auto packageEntry = std::make_shared<PackageEntry>();
		packageEntry->element = element;
		packageEntry->sticked = sticked;

		result.emplace_back(element->getFamilyKey(), packageEntry);
	};

	for (const auto& it: initialPackages)
	{
		generate(__fill_helper->getVertexPtr(it.first, it.second.version), false);
	}
	generate(__fill_helper->getDummyElementPtr(), true);

	return result;
}

void DependencyGraph::addUserRelationExpression(const UserRelationExpression& ure)
{
	__fill_helper->addUserRelationExpression(ure);
}

void DependencyGraph::unfoldElement(const Element* elementPtr)
{
	__fill_helper->unfoldElement(elementPtr);
}

const Element* DependencyGraph::getCorrespondingEmptyElement(const Element* elementPtr)
{
	auto versionVertex = dynamic_cast< const VersionVertex* >(elementPtr);
	if (!versionVertex)
	{
		fatal2i("getting corresponding empty element for non-version vertex");
	}
	const string& packageName = versionVertex->getPackageName();
	return __fill_helper->getVertexPtrForEmptyPackage(packageName);
}

}
}
}

