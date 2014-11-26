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

namespace std {

template< typename T >
class hash< pair<T, T> >
{
 public:
	size_t operator()(const pair<T, T>& p) const
	{
		return p_hasher(p.first) ^ p_hasher(p.second);
	}
 private:
	hash<T> p_hasher;
};

}

namespace cupt {
namespace internal {
namespace dependencygraph {

enum VertexTypePriority { Zero, SuggestsRelationExpression, RecommendsRelationExpression, WishRequest, TryRequest, StrongRelationExpression, MustRequest };

using cache::RelationExpression;
using std::make_pair;

BasicVertex::~BasicVertex()
{}

size_t BasicVertex::getTypePriority() const
{
	fatal2i("getting priority of '%s'", toString());
	return VertexTypePriority::Zero; // unreachable
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

const vector<Element>* BasicVertex::getRelatedElements() const
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

Element BasicVertex::getFamilyKey() const
{
	return this;
}


struct ExtendedBasicVertex: public BasicVertex
{
	virtual const string& getSpecificPackageName() const
	{
		fatal2i("getting getSpecificPackageName of '%s'", toString());
		__builtin_unreachable();
	}
};


VersionVertex::VersionVertex(const FamilyMap::iterator& it)
	: __related_element_ptrs_it(it)
{}

string VersionVertex::toString() const
{
	return getPackageName() + ' ' +
			(version ? version->versionString : "<not installed>");
}

const vector<Element>* VersionVertex::getRelatedElements() const
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
		return string(__("installed")) + ' ' + packageName + ' ' + version->versionString;
	}
	else
	{
		return string(__("removed")) + ' ' + packageName;
	}
}

Element VersionVertex::getFamilyKey() const
{
	return __related_element_ptrs_it->second.front();
}

typedef BinaryVersion::RelationTypes::Type RelationType;

struct RelationExpressionVertex: public ExtendedBasicVertex
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
			return VertexTypePriority::StrongRelationExpression;
		case BinaryVersion::RelationTypes::Recommends:
			return VertexTypePriority::RecommendsRelationExpression;
		case BinaryVersion::RelationTypes::Suggests:
			return VertexTypePriority::SuggestsRelationExpression;
		default:
			fatal2i("unsupported dependency type '%d'", int(dependencyType));
	}
	return VertexTypePriority::Zero; // unreacahble
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
		return VertexTypePriority::StrongRelationExpression;
	}
	bool isAnti() const
	{
		return true;
	}
	Unsatisfied::Type getUnsatisfiedType() const
	{
		return Unsatisfied::None;
	}
	const string& getSpecificPackageName() const
	{
		return specificPackageName;
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
	return isHard ? VertexTypePriority::StrongRelationExpression : VertexTypePriority::WishRequest;
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
	Element parent;

	string toString() const;
	const vector<Element>* getRelatedElements() const;
	Unsatisfied::Type getUnsatisfiedType() const;
};

string UnsatisfiedVertex::toString() const
{
	static const string u = "unsatisfied ";
	return u + parent->toString();
}

const vector<Element>* UnsatisfiedVertex::getRelatedElements() const
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

static inline uint8_t getTypePriorityForUserRequest(const UserRelationExpression& ure)
{
	if (ure.importance == RequestImportance::Must)
	{
		return VertexTypePriority::MustRequest;
	}
	else if (ure.importance == RequestImportance::Try)
	{
		return VertexTypePriority::TryRequest;
	}
	else
	{
		return VertexTypePriority::WishRequest;
	}
}

struct UserRelationExpressionVertex: public ExtendedBasicVertex
{
	bool invert;
	bool asAutoFlag;
	uint16_t typePriority;
	string annotation;
	string specificPackageName;

	UserRelationExpressionVertex(const UserRelationExpression& ure)
		: invert(ure.invert)
		, asAutoFlag(ure.asAuto)
		, typePriority(getTypePriorityForUserRequest(ure))
		, annotation(ure.annotation)
	{}
	size_t getTypePriority() const
	{
		return typePriority;
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
	const string& getSpecificPackageName() const
	{
		return specificPackageName;
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
	for (auto element: this->getVertices())
	{
		delete element;
	}
}

const vector<string>& __get_related_binary_package_names(const Cache& cache, const BinaryVersion* version)
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

	static vector<string> nullResult;
	return nullResult;
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

	typedef vector<Element> RelatedVertexPtrs;
	map< string, RelatedVertexPtrs > __package_name_to_vertex_ptrs;
	unordered_map<const void*, const VersionVertex*> __version_to_vertex_ptr;
	unordered_map< string, Element > __relation_expression_to_vertex_ptr;
	unordered_map< string, list<const ExtendedBasicVertex*> > __meta_anti_relation_expression_vertices;
	unordered_map< pair<string,string>, list<const SynchronizeVertex*> > __meta_synchronize_map;
	Element p_dummyElementPtr;

	set<Element> __unfolded_elements;

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
		p_dummyElementPtr = getVertexPtrForEmptyPackage("<user requests>", nullptr);
	}

 private:
	template< typename IsVertexAllowedT >
	const VersionVertex* getVertexPtr(const string& packageName, const BinaryVersion* version,
			const void* hashValue, const IsVertexAllowedT& isVertexAllowed,
			bool overrideChecks = false)
	{
		auto makeVertex = [this, &packageName, &version]() -> const VersionVertex*
		{
			auto relatedVertexPtrsIt = __package_name_to_vertex_ptrs.insert(
					{ packageName, RelatedVertexPtrs() }).first;
			auto vertexPtr(new VersionVertex(relatedVertexPtrsIt));
			vertexPtr->version = version;
			__dependency_graph.addVertex(vertexPtr);

			auto& relatedVertexes = relatedVertexPtrsIt->second;
			// keep first element (family key) always the same
			relatedVertexes.push_back(vertexPtr);

			return vertexPtr;
		};

		auto insertResult = __version_to_vertex_ptr.insert({ hashValue, nullptr });
		bool isNew = insertResult.second;
		const VersionVertex** elementPtrPtr = &insertResult.first->second;

		if ((isNew && isVertexAllowed()) || (overrideChecks && !*elementPtrPtr))
		{
			// needs new vertex
			*elementPtrPtr = makeVertex();
		}
		return *elementPtrPtr;
	}

 public:
	VersionElement getVertexPtrForVersion(const BinaryVersion* version, bool overrideChecks = false)
	{
		const auto& packageName = version->packageName;
		const void* hashValue = version;
		auto isVertexAllowed = [this, &packageName, &version]() -> bool
		{
			for (const BasicVertex* bv: __package_name_to_vertex_ptrs[packageName])
			{
				auto existingVersion = (static_cast< const VersionVertex* >(bv))->version;
				if (!existingVersion) continue;
				if (versionstring::getOriginal(version->versionString).equal(
						versionstring::getOriginal(existingVersion->versionString)))
				{
					if (std::equal(version->relations,
								version->relations + BinaryVersion::RelationTypes::Count,
								existingVersion->relations))
					{
						return false; // no reasons to allow this version dependency-wise
					}
				}
			}
			return true;
		};
		return getVertexPtr(packageName, version, hashValue, isVertexAllowed, overrideChecks);
	}

	Element getVertexPtrForEmptyPackage(const string& packageName, const BinaryPackage* package, bool /* overrideChecks */ = false)
	{
		const void* hashValue = package;
		auto isVertexAllowed = [this, &packageName]() -> bool
		{
			return __can_package_be_removed(packageName);
		};
		return getVertexPtr(packageName, nullptr, hashValue, isVertexAllowed, false);
	}

 private:
	void addEdgeCustom(Element fromVertexPtr, Element toVertexPtr)
	{
		__dependency_graph.addEdge(fromVertexPtr, toVertexPtr);
	}

	Element getVertexPtrForRelationExpression(const RelationExpression* relationExpressionPtr,
			const RelationType& dependencyType, bool* isNew)
	{
		auto hashKey = relationExpressionPtr->getHashString() + char('0' + dependencyType);
		Element& element = __relation_expression_to_vertex_ptr.insert(
				make_pair(std::move(hashKey), nullptr)).first->second;
		*isNew = !element;
		if (!element)
		{
			auto vertex(new RelationExpressionVertex);
			vertex->dependencyType = dependencyType;
			vertex->relationExpressionPtr = relationExpressionPtr;
			element = __dependency_graph.addVertex(vertex);
		}
		return element;
	}

 private:
	const ExtendedBasicVertex*& getOrCreateListSubElement(
			list<const ExtendedBasicVertex*>& l, const string& packageName)
	{
		for (auto& e: l)
		{
			if (e->getSpecificPackageName() == packageName)
			{
				return e;
			}
		}

		l.push_back(nullptr);
		return l.back();
	}

	void buildEdgesForAntiRelationExpression(
			list<const ExtendedBasicVertex*>* packageNameToSubElements,
			const vector< const BinaryVersion* > satisfyingVersions,
			const std::function< const ExtendedBasicVertex* (const string&) >& createVertex,
			bool overrideChecks = false)
	{
		for (auto satisfyingVersion: satisfyingVersions)
		{
			const string& packageName = satisfyingVersion->packageName;
			auto& subElement = getOrCreateListSubElement(*packageNameToSubElements, packageName);
			if (subElement) continue;

			subElement = createVertex(packageName);

			auto package = __dependency_graph.__cache.getBinaryPackage(packageName);
			if (!package) fatal2i("the binary package '%s' doesn't exist", packageName);

			for (auto pinnedVersion: __dependency_graph.__cache.getSortedPinnedVersions(package))
			{
				auto packageVersion = static_cast<const BinaryVersion*>(pinnedVersion.version);
				if (std::find(satisfyingVersions.begin(), satisfyingVersions.end(),
							packageVersion) == satisfyingVersions.end())
				{
					if (auto queuedVersionPtr = getVertexPtrForVersion(packageVersion, overrideChecks))
					{
						addEdgeCustom(subElement, queuedVersionPtr);
					}
				}
			}

			if (auto emptyPackageElementPtr = getVertexPtrForEmptyPackage(packageName, package, overrideChecks))
			{
				addEdgeCustom(subElement, emptyPackageElementPtr);
			}
		}
	}
	void processAntiRelation(const string& packageName,
			Element vertexPtr, const RelationExpression& relationExpression,
			BinaryVersion::RelationTypes::Type dependencyType)
	{
		auto hashKey = relationExpression.getHashString() + char('0' + dependencyType);
		static const list<const ExtendedBasicVertex*> emptyList;
		auto insertResult = __meta_anti_relation_expression_vertices.insert(
				make_pair(std::move(hashKey), emptyList));
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
				__dependency_graph.addVertex(subVertex);
				return subVertex;
			};
			auto satisfyingVersions = __dependency_graph.__cache.getSatisfyingVersions(relationExpression);
			buildEdgesForAntiRelationExpression(&packageNameToSubElements, satisfyingVersions, createVertex);
		}
		for (const auto& it: packageNameToSubElements)
		{
			if (it->getSpecificPackageName() == packageName)
			{
				continue; // doesn't conflict with itself
			}
			addEdgeCustom(vertexPtr, it);
		}
	}

	void processForwardRelation(const BinaryVersion* version,
			Element vertexPtr, const RelationExpression& relationExpression,
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
			if (auto queuedVersionPtr = getVertexPtrForVersion(*satisfyingVersionIt))
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

	void processSynchronizations(const BinaryVersion*& version, Element vertexPtr)
	{
		auto hashKey = make_pair(version->sourcePackageName, version->sourceVersionString);
		static const list<const SynchronizeVertex*> emptyList;
		auto insertResult = __meta_synchronize_map.insert(make_pair(hashKey, emptyList));
		bool isNewMetaVertex = insertResult.second;
		auto& subElementPtrs = insertResult.first->second;

		if (isNewMetaVertex)
		{
			for (const auto& packageName: __get_related_binary_package_names(__dependency_graph.__cache, version))
			{
				auto package = __dependency_graph.__cache.getBinaryPackage(packageName);
				if (!package) continue;

				auto syncVertex = new SynchronizeVertex(__synchronize_level > 1);
				syncVertex->targetPackageName = packageName;
				__dependency_graph.addVertex(syncVertex);

				for (auto relatedVersion: *package)
				{
					if (relatedVersion->sourceVersionString == version->sourceVersionString)
					{
						if (auto relatedVersionVertexPtr = getVertexPtrForVersion(relatedVersion))
						{
							addEdgeCustom(syncVertex, relatedVersionVertexPtr);
						}
					}
				}

				if (auto emptyVersionPtr = getVertexPtrForEmptyPackage(packageName, package))
				{
					addEdgeCustom(syncVertex, emptyVersionPtr);
				}

				if (__synchronize_level == 1) // soft
				{
					auto unsatisfiedVertex = new UnsatisfiedVertex;
					unsatisfiedVertex->parent = syncVertex;
					addEdgeCustom(syncVertex, __dependency_graph.addVertex(unsatisfiedVertex));
				}

				subElementPtrs.push_back(syncVertex);
			}
		}

		for (const auto& subElement: subElementPtrs)
		{
			if (subElement->targetPackageName == version->packageName)
			{
				continue; // don't synchronize with itself, it's always satisfied
			}
			addEdgeCustom(vertexPtr, subElement);
		}
	}

	Element createCustomUnsatisfiedElement(Element parent, const RequestImportance& importance)
	{
		auto vertex = new CustomUnsatisfiedVertex(importance);
		vertex->parent = parent;
		return __dependency_graph.addVertex(vertex);
	}

 public:
	void unfoldElement(Element element)
	{
		if (!__unfolded_elements.insert(element).second)
		{
			return; // processed already
		}
		auto versionElement = dynamic_cast<VersionElement>(element);
		if (!versionElement)
		{
			return; // nothing to process
		}

		// persistent one
		auto version = versionElement->version;
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
					processAntiRelation(version->packageName, element, relationExpression, dependencyType);
				}
				else
				{
					processForwardRelation(version, element, relationExpression, dependencyType);
				}
			}
		}

		if (__synchronize_level && !version->isInstalled())
		{
			processSynchronizations(version, element);
		}
	}

	Element getDummyElementPtr() const
	{
		return p_dummyElementPtr;
	}

	void addUserRelationExpression(const UserRelationExpression& ure)
	{
		Element unsatisfiedElement = nullptr;

		auto createVertex = [&](const string& packageName) -> const ExtendedBasicVertex*
		{
			auto vertex = new UserRelationExpressionVertex(ure);
			vertex->specificPackageName = packageName;
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
				addEdgeCustom(vertex, getVertexPtrForVersion(version, true));
			}
		}
		else
		{
			list<const ExtendedBasicVertex*> subElements;
			buildEdgesForAntiRelationExpression(&subElements, satisfyingVersions, createVertex, true);
		}
	}
};

vector< pair< dg::Element, shared_ptr< const PackageEntry > > > DependencyGraph::fill(
		const map< string, const BinaryVersion* >& oldPackages)
{
	__fill_helper.reset(new DependencyGraph::FillHelper(*this, oldPackages));

	for (const auto& item: oldPackages)
	{
		const auto& oldVersion = item.second;
		p_populatePackage(oldVersion->packageName);
	}

	return p_generateSolutionElements(oldPackages);
}

void DependencyGraph::p_populatePackage(const string& packageName)
{
	auto package = __cache.getBinaryPackage(packageName);

	auto versions = package->getVersions();
	std::stable_sort(versions.begin(), versions.end(),
			[](const BinaryVersion* left, const BinaryVersion* right)
			{
				return compareVersionStrings(left->versionString, right->versionString) > 0;
			});
	for (auto version: versions)
	{
		__fill_helper->getVertexPtrForVersion(version);
	}

	__fill_helper->getVertexPtrForEmptyPackage(packageName, package); // also, empty one
}

vector< pair< dg::Element, shared_ptr< const PackageEntry > > > DependencyGraph::p_generateSolutionElements(
		const map< string, const BinaryVersion* >& oldPackages)
{
	vector< pair< Element, shared_ptr< const PackageEntry > > > result;

	auto generate = [&result](Element element, bool sticked)
	{
		auto packageEntry = std::make_shared<PackageEntry>();
		packageEntry->element = element;
		packageEntry->sticked = sticked;

		result.emplace_back(element->getFamilyKey(), packageEntry);
	};

	for (const auto& it: oldPackages)
	{
		generate(__fill_helper->getVertexPtrForVersion(it.second), false);
	}
	generate(__fill_helper->getDummyElementPtr(), true);

	return result;
}

void DependencyGraph::addUserRelationExpression(const UserRelationExpression& ure)
{
	__fill_helper->addUserRelationExpression(ure);
}

void DependencyGraph::unfoldElement(Element element)
{
	__fill_helper->unfoldElement(element);
}

Element DependencyGraph::getCorrespondingEmptyElement(Element element)
{
	auto versionVertex = dynamic_cast<VersionElement>(element);
	if (!versionVertex)
	{
		fatal2i("getting corresponding empty element for non-version vertex");
	}
	const string& packageName = versionVertex->getPackageName();
	auto package = __cache.getBinaryPackage(packageName);
	return __fill_helper->getVertexPtrForEmptyPackage(packageName, package);
}

}
}
}

