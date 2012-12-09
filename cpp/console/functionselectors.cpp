/**************************************************************************
*   Copyright (C) 2012 by Eugene V. Lyubimkin                             *
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
#include <common/regex.hpp>

#include <cupt/cache/releaseinfo.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/sourcepackage.hpp>
#include <cupt/cache/binaryversion.hpp>
#include <cupt/cache/sourceversion.hpp>
#include <cupt/system/state.hpp>

#include "functionselectors.hpp"

FunctionalSelector::Query::Query()
{}

FunctionalSelector::Query::~Query()
{}

namespace {

typedef FunctionalSelector::Query FS; // rename to FSQ
typedef const Version* SPCV; // former shared_ptr< const Version >
typedef list< SPCV > FSResult;
typedef BinaryVersion::RelationTypes BRT;
typedef SourceVersion::RelationTypes SRT;

const RelationLine& getRelationLine(const Version* v, BRT::Type relationType)
{
	return static_cast< const BinaryVersion* >(v)->relations[relationType];
}
RelationLine getRelationLine(const Version* v, SRT::Type relationType)
{
	return static_cast< const SourceVersion* >(v)->relations[relationType].toRelationLine("");
}

template < typename RelationType >
struct IsBinary
{};
template <>
struct IsBinary< BRT::Type >
{
	static const bool value = true;
};
template <>
struct IsBinary< SRT::Type >
{
	static const bool value = false;
};


bool __spcv_less(const Cache& cache, const SPCV& left, const SPCV& right)
{
	if (left->packageName > right->packageName)
	{
		return true;
	}
	if (left->packageName < right->packageName)
	{
		return false;
	}
	auto leftPin = cache.getPin(left);
	auto rightPin = cache.getPin(right);
	if (leftPin < rightPin)
	{
		return true;
	}
	if (leftPin > rightPin)
	{
		return false;
	}
	return left->versionString < right->versionString;
}
struct SpcvGreater
{
 private:
	const Cache& __cache;
 public:
	SpcvGreater(const Cache& cache) : __cache(cache) {}
	bool operator()(const SPCV& left, const SPCV& right)
	{
		return __spcv_less(__cache, right, left);
	}
};

class VersionSetGetter
{
	bool __binary; // source if false
	const Cache& __cache;
	mutable FSResult* __cached_all_versions;

	vector< string > __get_package_names() const
	{
		auto result = __binary ? __cache.getBinaryPackageNames() : __cache.getSourcePackageNames();
		std::sort(result.begin(), result.end());
		return result;
	}
	const Package* __get_package(const string& packageName) const
	{
		return __binary ? (const Package*)__cache.getBinaryPackage(packageName)
				: (const Package*)__cache.getSourcePackage(packageName);
	}
	void __add_package_to_result(const string& packageName, FSResult* result) const
	{
		// we call getSortedPinnedVersions() to place versions of the same package in the preference order
		for (auto&& pinnedVersion: __cache.getSortedPinnedVersions(__get_package(packageName)))
		{
			result->emplace_back(std::move(pinnedVersion.version));
		}
	}
 public:
	explicit VersionSetGetter(const Cache& cache, bool binary)
		: __binary(binary), __cache(cache), __cached_all_versions(NULL)
	{}
	const FSResult& getAll() const
	{
		if (!__cached_all_versions)
		{
			__cached_all_versions = new FSResult;
			for (const string& packageName: __get_package_names())
			{
				__add_package_to_result(packageName, __cached_all_versions);
			}
		}
		return *__cached_all_versions;
	}
	FSResult get(const sregex& regex) const
	{
		smatch m;
		FSResult result;
		for (const string& packageName: __get_package_names())
		{
			if (!regex_match(packageName, m, regex))
			{
				continue;
			}
			__add_package_to_result(packageName, &result);
		}
		return result;
	}
	~VersionSetGetter()
	{
		delete __cached_all_versions;
	}
};

class VersionSet
{
	const VersionSetGetter* __binary_getter;
	const VersionSetGetter* __source_getter;
	const VersionSetGetter* __current_getter;
	bool __filtered;
	FSResult __versions;
	std::map< string, FSResult > __variables;

	VersionSet(const VersionSet& from, FSResult&& versions)
		: __filtered(true), __versions(std::move(versions)), __variables(from.__variables)
	{}
 public:
	explicit VersionSet(const VersionSetGetter* binaryGetter, const VersionSetGetter* sourceGetter)
		: __binary_getter(binaryGetter), __source_getter(sourceGetter), __filtered(false)
	{}
	VersionSet generate(FSResult&& versions) const
	{
		return VersionSet(*this, std::move(versions));
	}
	void selectGetterType(bool binary)
	{
		__current_getter = binary ? __binary_getter : __source_getter;
	}
	const FSResult& get() const
	{
		if (__filtered)
		{
			return __versions;
		}
		else
		{
			return __current_getter->getAll();
		}
	}
	FSResult get(const sregex& regex) const
	{
		if (__filtered)
		{
			smatch m;
			FSResult result(__versions);
			result.remove_if([&regex, &m](const SPCV& version)
			{
				return !regex_match(version->packageName, m, regex);
			});
			return result;
		}
		else
		{
			return __current_getter->get(regex);
		}
	}
	void setVariable(const string& name, FSResult&& versions)
	{
		__variables[name] = std::move(versions);
	}
	const FSResult& getFromVariable(const string& name) const
	{
		auto it = __variables.find(name);
		if (it == __variables.end())
		{
			fatal2("the variable '%s' is not defined", name);
		}
		return it->second;
	}
	VersionSet getUnfiltered() const
	{
		VersionSet result(__binary_getter, __source_getter);
		result.__variables = this->__variables;
		result.__current_getter = this->__current_getter;
		return result;
	}
	bool isFiltered() const
	{
		return __filtered;
	}
};

struct Context
{
	const Cache& cache;
	ReverseDependsIndex< BinaryVersion > reverseIndex;
	ReverseDependsIndex< SourceVersion > reverseBuildIndex;

	Context(const Cache& cache_)
		: cache(cache_), reverseIndex(cache), reverseBuildIndex(cache)
	{}
	SpcvGreater getSorter() const
	{
		return SpcvGreater(cache);
	}
	void mergeFsResults(FSResult* main, FSResult&& other)
	{
		main->merge(std::move(other), getSorter());
		main->unique();
	}
	FSResult filterThrough(const FSResult& versions, const VersionSet& allowedSet)
	{
		if (allowedSet.isFiltered())
		{
			const auto& allowedVersions = allowedSet.get();
			FSResult result;
			std::set_intersection(allowedVersions.begin(), allowedVersions.end(),
					versions.begin(), versions.end(),
					std::back_inserter(result), getSorter());
			return result;
		}
		else
		{
			return versions;
		}
	}
};

class CommonFS: public FS
{
 public:
	typedef vector< string > Arguments;
	virtual FSResult select(Context&, const VersionSet& from) const = 0;
};

unique_ptr< CommonFS > internalParseFunctionQuery(const string& query, bool binary);

void __require_n_arguments(const CommonFS::Arguments& arguments, size_t n)
{
	if (arguments.size() != n)
	{
		fatal2(__("the function requires exactly %zu arguments"), n);
	}
}

class BestFS: public CommonFS
{
	unique_ptr< CommonFS > __leaf_fs;
 public:
	BestFS(bool binary, const Arguments& arguments)
	{
		__require_n_arguments(arguments, 1);
		__leaf_fs = internalParseFunctionQuery(arguments[0], binary);
	}
	FSResult select(Context& context, const VersionSet& from) const
	{
		auto result = __leaf_fs->select(context, from);
		result.unique([](const SPCV& left, const SPCV& right) { return left->packageName == right->packageName; });
		return result;
	}
};

class DefineVariableFS: public CommonFS
{
	string __name;
	unique_ptr< CommonFS > __value_fs;
	unique_ptr< CommonFS > __leaf_fs;
 public:
	DefineVariableFS(bool binary, const Arguments& arguments)
	{
		__require_n_arguments(arguments, 3);
		__name = arguments[0];
		__value_fs = internalParseFunctionQuery(arguments[1], binary);
		__leaf_fs = internalParseFunctionQuery(arguments[2], binary);
	}
	FSResult select(Context& context, const VersionSet& from) const
	{
		VersionSet modifiedFrom(from);
		modifiedFrom.setVariable(__name,
				__value_fs->select(context, from.getUnfiltered()));
		return __leaf_fs->select(context, modifiedFrom);
	}
};

class ExtractVariableFS: public CommonFS
{
	const string __name;
 public:
	ExtractVariableFS(const string& name, const Arguments& arguments)
		: __name(name)
	{
		__require_n_arguments(arguments, 0);
	}
	FSResult select(Context& context, const VersionSet& from) const
	{
		return context.filterThrough(from.getFromVariable(__name), from);
	}
};

class AlgeFS: public CommonFS
{
 protected:
	list< unique_ptr< CommonFS > > _leaves;
 public:
	// postcondition: _leaves are not empty
	AlgeFS(bool binary, const Arguments& arguments)
	{
		if (arguments.empty())
		{
			fatal2(__("the function should have at least one argument"));
		}
		for (const auto& argument: arguments)
		{
			_leaves.push_back(internalParseFunctionQuery(argument, binary));
		}
	}
};

class AndFS: public AlgeFS
{
 public:
	AndFS(bool binary, const Arguments& arguments)
		: AlgeFS(binary, arguments)
	{}
	FSResult select(Context& context, const VersionSet& from) const
	{
		auto result = _leaves.front()->select(context, from);
		for (auto it = ++_leaves.begin(); it != _leaves.end(); ++it)
		{
			result = (*it)->select(context, from.generate(std::move(result)));
		}
		return result;
	}
};

class NotFS: public AlgeFS
{
	static const Arguments& __check_and_return_arguments(const Arguments& arguments)
	{
		__require_n_arguments(arguments, 1);
		return arguments;
	}
 public:
	NotFS(bool binary, const Arguments& arguments)
		: AlgeFS(binary, __check_and_return_arguments(arguments))
	{}
	FSResult select(Context& context, const VersionSet& from) const
	{
		const auto& fromVersions = from.get();
		auto notVersions = _leaves.front()->select(context, from);
		FSResult result;
		std::set_difference(fromVersions.begin(), fromVersions.end(),
				notVersions.begin(), notVersions.end(),
				std::back_inserter(result), context.getSorter());
		return result;
	}
};

class XorFS: public AlgeFS
{
	static const Arguments& __check_and_return_arguments(const Arguments& arguments)
	{
		__require_n_arguments(arguments, 2);
		return arguments;
	}
 public:
	XorFS(bool binary, const Arguments& arguments)
		: AlgeFS(binary, __check_and_return_arguments(arguments))
	{}
	FSResult select(Context& context, const VersionSet& from) const
	{
		auto leftVersions = _leaves.front()->select(context, from);
		auto rightVersions = _leaves.back()->select(context, from);
		FSResult result;
		std::set_symmetric_difference(leftVersions.begin(), leftVersions.end(),
				rightVersions.begin(), rightVersions.end(),
				std::back_inserter(result), context.getSorter());
		return result;
	}
};

// for determining, is function selector binary or source
class BinaryTagDummyFS: public CommonFS
{
	unique_ptr< CommonFS > __real_fs;
 public:
	BinaryTagDummyFS(unique_ptr< CommonFS >&& realFS)
		: __real_fs(std::move(realFS))
	{}
	FSResult select(Context& context, const VersionSet& from) const
	{
		return __real_fs->select(context, from);
	}
};

class OrFS: public AlgeFS
{
 public:
	OrFS(bool binary, const Arguments& arguments)
		: AlgeFS(binary, arguments)
	{}
	FSResult select(Context& context, const VersionSet& from) const
	{
		auto result = _leaves.front()->select(context, from);
		for (auto it = ++_leaves.begin(); it != _leaves.end(); ++it)
		{
			auto part = (*it)->select(context, from);
			context.mergeFsResults(&result, std::move(part));
		}
		return result;
	}
};

class PredicateFS: public CommonFS
{
 protected:
	virtual bool _match(const Cache&, const SPCV&) const = 0;
 public:
	FSResult select(Context& context, const VersionSet& from) const
	{
		FSResult result = from.get();
		result.remove_if([this, &context](const SPCV& version) { return !this->_match(context.cache, version); });
		return result;
	}
};

sregex __parse_regex(const string& input)
{
	try
	{
		return sregex::compile(input, regex_constants::optimize);
	}
	catch (regex_error&)
	{
		fatal2(__("regular expression '%s' is not valid"), input);
		__builtin_unreachable();
	}
}

sregex __get_regex_from_arguments(const CommonFS::Arguments& arguments)
{
	__require_n_arguments(arguments, 1);
	return __parse_regex(arguments[0]);
}

class RegexMatcher
{
	sregex __regex;
	mutable smatch __m;

 public:
	RegexMatcher(const CommonFS::Arguments& arguments)
		: __regex(__get_regex_from_arguments(arguments))
	{}
	bool match(const string& input) const
	{
		return regex_match(input, __m, __regex);
	}
};

class PackageNameFS: public CommonFS
{
	sregex __regex;
 public:
	PackageNameFS(const Arguments& arguments)
		: __regex(__get_regex_from_arguments(arguments))
	{}
	FSResult select(Context&, const VersionSet& from) const
	{
		return from.get(__regex);
	}
};

class RegexMatchBaseFS: public PredicateFS
{
 protected:
	RegexMatcher _matcher;
 public:
	RegexMatchBaseFS(const Arguments& arguments)
		: _matcher(arguments)
	{}
};

class RegexMatchFS: public RegexMatchBaseFS
{
	std::function< string (const Cache&, const SPCV&) > __get_attribute;
 public:
	RegexMatchFS(decltype(__get_attribute) getAttribute, const Arguments& arguments)
		: RegexMatchBaseFS(arguments), __get_attribute(getAttribute)
	{}
 protected:
	bool _match(const Cache& cache, const SPCV& version) const
	{
		return _matcher.match(__get_attribute(cache, version));
	}
};

class SourceRegexMatchFS: public RegexMatchBaseFS
{
	std::function< string (const Version::Source&) > __get_source_attribute;
 public:
	SourceRegexMatchFS(decltype(__get_source_attribute) getter, const Arguments& arguments)
		: RegexMatchBaseFS(arguments), __get_source_attribute(getter)
	{}
 protected:
	bool _match(const Cache&, const SPCV& version) const
	{
		for (const auto& source: version->sources)
		{
			if (_matcher.match(__get_source_attribute(source)))
			{
				return true;
			}
		}
		return false;
	}
};

class ProvidesFS: public RegexMatchBaseFS
{
 public:
	ProvidesFS(const Arguments& arguments)
		: RegexMatchBaseFS(arguments)
	{}
 protected:
	bool _match(const Cache&, const SPCV& v) const
	{
		auto version = static_cast< const BinaryVersion* >(v);
		for (const string& virtualPackageName: version->provides)
		{
			if (_matcher.match(virtualPackageName))
			{
				return true;
			}
		}
		return false;
	}
};

class UploadersFS: public RegexMatchBaseFS
{
 public:
	UploadersFS(const Arguments& arguments)
		: RegexMatchBaseFS(arguments)
	{}
 protected:
	bool _match(const Cache&, const SPCV& v) const
	{
		auto version = static_cast< const SourceVersion* >(v);
		for (const string& uploader: version->uploaders)
		{
			if (_matcher.match(uploader)) return true;
		}
		return false;
	}
};

class BoolMatchFS: public PredicateFS
{
	std::function< bool (const Cache&, const SPCV&) > __get_attribute;
 public:
	BoolMatchFS(decltype(__get_attribute) getAttribute, const Arguments& arguments)
		: __get_attribute(getAttribute)
	{
		__require_n_arguments(arguments, 0);
	}
	bool _match(const Cache& cache, const SPCV& version) const
	{
		return __get_attribute(cache, version);
	}
};

class OtherFieldRegexMatchFS: public RegexMatchFS
{
	string __field_name;
	static string __extract_second_argument(const Arguments& arguments)
	{
		__require_n_arguments(arguments, 2);
		return arguments[1];
	}
 public:
	OtherFieldRegexMatchFS(const Arguments& arguments)
		: RegexMatchFS([this](const Cache&, const SPCV& version) -> string
				{
					if (!version->others)
					{
						return string();
					}
					auto it = version->others->find(this->__field_name);
					if (it != version->others->end())
					{
						return it->second;
					}
					else
					{
						return string();
					}
				}, { __extract_second_argument(arguments) }),
		__field_name(arguments[0])
	{}
};

class TransformFS: public CommonFS
{
	unique_ptr< CommonFS > __leaf;
	bool __binary;
 protected:
	virtual FSResult _transform(Context&, const SPCV& version) const = 0;
 public:
	TransformFS(bool binary, const Arguments& arguments)
		: __binary(binary)
	{
		__require_n_arguments(arguments, 1);
		__leaf = internalParseFunctionQuery(arguments[0], binary);
	}
	FSResult select(Context& context, const VersionSet& from) const
	{
		FSResult allTransformed;
		auto newVersionSet = from.getUnfiltered();
		newVersionSet.selectGetterType(__binary);
		for (const auto& version: __leaf->select(context, newVersionSet))
		{
			auto transformedList = _transform(context, version);
			context.mergeFsResults(&allTransformed, std::move(transformedList));
		}
		return context.filterThrough(allTransformed, from);
	}
};

class RecursiveFS: public CommonFS
{
	string __variable_name;
	unique_ptr< CommonFS > __initial_variable_value_fs;
	unique_ptr< CommonFS > __iterating_fs;

 public:
	RecursiveFS(bool binary, const Arguments& arguments)
	{
		__require_n_arguments(arguments, 3);
		__variable_name = arguments[0];
		__initial_variable_value_fs = internalParseFunctionQuery(arguments[1], binary);
		__iterating_fs = internalParseFunctionQuery(arguments[2], binary);
	}
	FSResult select(Context& context, const VersionSet& from) const
	{
		FSResult result;

		FSResult variableValue = __initial_variable_value_fs->select(context, from.getUnfiltered());
		size_t previousResultSize;
		do
		{
			previousResultSize = result.size();

			VersionSet iterationSet = from.getUnfiltered();
			iterationSet.setVariable(__variable_name, std::move(variableValue));
			context.mergeFsResults(&result, __iterating_fs->select(context, iterationSet));

			variableValue = result;
		}
		while (result.size() != previousResultSize);

		return context.filterThrough(result, from);
	}
};

template < typename RelationType >
class DependencyFS: public TransformFS
{
	const RelationType __relation_type;
 public:
	DependencyFS(RelationType relationType, const Arguments& arguments)
		: TransformFS(IsBinary<RelationType>::value, arguments), __relation_type(relationType)
	{}
 protected:
	FSResult _transform(Context& context, const SPCV& version) const
	{
		auto sorter = context.getSorter();
		FSResult result;

		for (const auto& relationExpression: getRelationLine(version, __relation_type))
		{
			auto satisfyingVersions = context.cache.getSatisfyingVersions(relationExpression);
			std::sort(satisfyingVersions.begin(), satisfyingVersions.end(), sorter);
			list< SPCV > sortedList;
			std::move(satisfyingVersions.begin(), satisfyingVersions.end(),
					std::back_inserter(sortedList));
			context.mergeFsResults(&result, std::move(sortedList));
		}
		return result;
	}
};

class ReverseDependencyFS: public TransformFS
{
	BRT::Type __relation_type;
 public:
	ReverseDependencyFS(BRT::Type relationType, const Arguments& arguments)
		: TransformFS(true, arguments), __relation_type(relationType)
	{}
 protected:
	FSResult _transform(Context& context, const SPCV& version) const
	{
		context.reverseIndex.add(__relation_type);

		FSResult result;

		auto binaryVersion = static_cast< const BinaryVersion* >(version);
		context.reverseIndex.foreachReverseDependency(binaryVersion, __relation_type,
				[&context, &result](const BinaryVersion* reverseVersion, const RelationExpression&)
				{
					context.mergeFsResults(&result, { reverseVersion });
				});

		return result;
	}
};

class ReverseBuildDependencyFS: public TransformFS
{
	SRT::Type __relationType;
 public:
	ReverseBuildDependencyFS(SRT::Type relationType, const Arguments& arguments)
		: TransformFS(true, arguments), __relationType(relationType)
	{}
 protected:
	FSResult _transform(Context& context, const SPCV& version) const
	{
		context.reverseBuildIndex.add(__relationType);

		FSResult result;

		auto binaryVersion = static_cast< const BinaryVersion* >(version);
		context.reverseBuildIndex.foreachReverseDependency(binaryVersion, __relationType,
				[&context, &result](const SourceVersion* reverseVersion, const RelationExpression&)
				{
					context.mergeFsResults(&result, { reverseVersion });
				});
		return result;
	}
};

class PackageIsInstalledFS: public PredicateFS
{
 protected:
	bool _match(const Cache& cache, const SPCV& version) const
	{
		return isPackageInstalled(cache, version->packageName);
	}
 public:
	PackageIsInstalledFS(const Arguments& arguments)
	{
		__require_n_arguments(arguments, 0);
	}
};

class PackageIsAutoInstalledFS: public PredicateFS
{
 protected:
	bool _match(const Cache& cache, const SPCV& version) const
	{
		return cache.isAutomaticallyInstalled(version->packageName);
	}
 public:
	PackageIsAutoInstalledFS(const Arguments& arguments)
	{
		__require_n_arguments(arguments, 0);
	}
};

///

namespace attr
{
	string priority(const Cache&, const SPCV& version)
	{
		return Version::Priorities::strings[version->priority];
	}
}

CommonFS* constructFSByName(const string& functionName, const CommonFS::Arguments& arguments, bool binary)
{
	#define VERSION_MEMBER(member) [](const Cache&, const SPCV& version) { return version-> member; }
	#define VERSION_RELEASE_MEMBER(member) [](const Version::Source& source) { return source.release-> member; }
	#define BINARY_VERSION_MEMBER(member) [](const Cache&, const SPCV& version) \
			{ return static_cast< const BinaryVersion* >(version)-> member; }
	#define CONSTRUCT_FS(name, code) if (functionName == name) { return new code; }
	#define CONSTRUCT_RELEASE_MEMBER_FS(name, member) \
			CONSTRUCT_FS(name, SourceRegexMatchFS(VERSION_RELEASE_MEMBER(member), arguments))

	// variables
	if (functionName.front() == '_')
	{
		return new ExtractVariableFS(functionName, arguments);
	}
	CONSTRUCT_FS("with", DefineVariableFS(binary, arguments))
	CONSTRUCT_FS("recursive", RecursiveFS(binary, arguments))
	// logic
	CONSTRUCT_FS("and", AndFS(binary, arguments))
	CONSTRUCT_FS("or", OrFS(binary, arguments))
	CONSTRUCT_FS("not", NotFS(binary, arguments))
	CONSTRUCT_FS("xor", XorFS(binary, arguments))
	// narrowing/widening
	CONSTRUCT_FS("best", BestFS(binary, arguments))
	// common
	CONSTRUCT_FS("package:name", PackageNameFS(arguments))
	CONSTRUCT_FS("version", RegexMatchFS(VERSION_MEMBER(versionString), arguments))
	CONSTRUCT_FS("maintainer", RegexMatchFS(VERSION_MEMBER(maintainer), arguments))
	CONSTRUCT_FS("priority", RegexMatchFS(attr::priority, arguments))
	CONSTRUCT_FS("section", RegexMatchFS(VERSION_MEMBER(section), arguments))
	CONSTRUCT_FS("trusted", BoolMatchFS(VERSION_MEMBER(isVerified()), arguments))
	CONSTRUCT_FS("field", OtherFieldRegexMatchFS(arguments))
	CONSTRUCT_RELEASE_MEMBER_FS("release:archive", archive)
	CONSTRUCT_RELEASE_MEMBER_FS("release:codename", codename)
	CONSTRUCT_RELEASE_MEMBER_FS("release:component", component)
	CONSTRUCT_RELEASE_MEMBER_FS("release:version", version)
	CONSTRUCT_RELEASE_MEMBER_FS("release:vendor", vendor)
	CONSTRUCT_RELEASE_MEMBER_FS("release:origin", baseUri)
	if (binary)
	{
		CONSTRUCT_FS("source-package", RegexMatchFS(BINARY_VERSION_MEMBER(sourcePackageName), arguments))
		CONSTRUCT_FS("source-version", RegexMatchFS(BINARY_VERSION_MEMBER(sourceVersionString), arguments))
		CONSTRUCT_FS("essential", BoolMatchFS(BINARY_VERSION_MEMBER(essential), arguments))
		CONSTRUCT_FS("installed", BoolMatchFS(BINARY_VERSION_MEMBER(isInstalled()), arguments))
		CONSTRUCT_FS("description", RegexMatchFS(BINARY_VERSION_MEMBER(description), arguments))
		CONSTRUCT_FS("package:installed", PackageIsInstalledFS(arguments))
		CONSTRUCT_FS("package:automatically-installed", PackageIsAutoInstalledFS(arguments))
		// relations
		CONSTRUCT_FS("pre-depends", DependencyFS<BRT::Type>(BRT::PreDepends, arguments))
		CONSTRUCT_FS("depends", DependencyFS<BRT::Type>(BRT::Depends, arguments))
		CONSTRUCT_FS("recommends", DependencyFS<BRT::Type>(BRT::Recommends, arguments))
		CONSTRUCT_FS("suggests", DependencyFS<BRT::Type>(BRT::Suggests, arguments))
		CONSTRUCT_FS("conflicts", DependencyFS<BRT::Type>(BRT::Conflicts, arguments))
		CONSTRUCT_FS("breaks", DependencyFS<BRT::Type>(BRT::Breaks, arguments))
		CONSTRUCT_FS("replaces", DependencyFS<BRT::Type>(BRT::Replaces, arguments))
		CONSTRUCT_FS("enhances", DependencyFS<BRT::Type>(BRT::Enhances, arguments))
		CONSTRUCT_FS("reverse-pre-depends", ReverseDependencyFS(BRT::PreDepends, arguments))
		CONSTRUCT_FS("reverse-depends", ReverseDependencyFS(BRT::Depends, arguments))
		CONSTRUCT_FS("reverse-recommends", ReverseDependencyFS(BRT::Recommends, arguments))
		CONSTRUCT_FS("reverse-suggests", ReverseDependencyFS(BRT::Suggests, arguments))
		CONSTRUCT_FS("reverse-conflicts", ReverseDependencyFS(BRT::Conflicts, arguments))
		CONSTRUCT_FS("reverse-breaks", ReverseDependencyFS(BRT::Breaks, arguments))
		CONSTRUCT_FS("reverse-enhances", ReverseDependencyFS(BRT::Enhances, arguments))
		CONSTRUCT_FS("reverse-replaces", ReverseDependencyFS(BRT::Replaces, arguments))

		CONSTRUCT_FS("provides", ProvidesFS(arguments))

		CONSTRUCT_FS("build-depends", DependencyFS<SRT::Type>(SRT::BuildDepends, arguments))
		CONSTRUCT_FS("build-depends-indep", DependencyFS<SRT::Type>(SRT::BuildDependsIndep, arguments))
		CONSTRUCT_FS("build-conflicts", DependencyFS<SRT::Type>(SRT::BuildConflicts, arguments))
		CONSTRUCT_FS("build-conflicts-indep", DependencyFS<SRT::Type>(SRT::BuildConflictsIndep, arguments))
	}
	else
	{
		CONSTRUCT_FS("uploaders", UploadersFS(arguments))

		CONSTRUCT_FS("reverse-build-depends", ReverseBuildDependencyFS(SRT::BuildDepends, arguments))
		CONSTRUCT_FS("reverse-build-depends-indep", ReverseBuildDependencyFS(SRT::BuildDependsIndep, arguments))
		CONSTRUCT_FS("reverse-build-conflicts", ReverseBuildDependencyFS(SRT::BuildConflicts, arguments))
		CONSTRUCT_FS("reverse-build-conflicts-indep", ReverseBuildDependencyFS(SRT::BuildConflictsIndep, arguments))
	}

	fatal2(__("unknown %s selector function '%s'"), binary ? __("binary") : __("source"), functionName);
	__builtin_unreachable();
}

vector< string > split(const string& input, const char delimiter = ',')
{
	if (input.empty())
	{
		return {};
	}
	vector< string > result;
	size_t argumentStartPosition = 0;
	size_t position = 0;
	size_t level = 0;

	char delimiters[] = "d()/";
	delimiters[0] = delimiter;

	while (position = input.find_first_of(delimiters, position), position != string::npos)
	{
		if (input[position] == delimiter && level == 0)
		{
			result.push_back(input.substr(argumentStartPosition, position - argumentStartPosition));
			argumentStartPosition = position+1;
		}
		else switch (input[position])
		{
			case '(':
				++level;
				break;
			case ')':
				if (level == 0)
				{
					fatal2(__("unexpected closing bracket ')' after '%s'"), input.substr(0, position));
				}
				--level;
				break;
			case '/': // quoting
				position = input.find('/', position+1);
				if (position == string::npos)
				{
					fatal2(__("unable to find closing quoting character '/'"));
				}
		}
		++position;
	}
	if (level != 0)
	{
		fatal2(__("too few closing brackets"));
	}
	result.push_back(input.substr(argumentStartPosition, input.size() - argumentStartPosition));
	return result;
}

void trim(string& s)
{
	const char* trimmedCharacters = " \t\n";
	if (s.size() > 0)
	{
		auto firstValuablePosition = s.find_first_not_of(trimmedCharacters);
		auto lastValuablePosition = s.find_last_not_of(trimmedCharacters);
		s = s.substr(firstValuablePosition, lastValuablePosition - firstValuablePosition + 1);
	}
}

void stripArgumentQuotes(string& argument)
{
	if (argument.size() >= 2)
	{
		if (argument.front() == '/' && argument.back() == '/')
		{
			argument = argument.substr(1, argument.size()-2);
		}
	}
}

void processNonTrivialAliases(string* functionNamePtr, vector< string >* argumentsPtr)
{
	auto getUniqueVariableName = []()
	{
		static size_t anonymousVariableId = 0;
		return format2("__anon%zu", anonymousVariableId++);
	};

	if (*functionNamePtr == "package-with-installed-dependencies")
	{
		__require_n_arguments(*argumentsPtr, 1);
		*functionNamePtr = "recursive";
		auto variableName = getUniqueVariableName();
		auto recursiveExpression = format2(
				"best( fmap(%s, Ypd,Yd,Yr) & Pi )", variableName);
		*argumentsPtr = { variableName, argumentsPtr->front(), recursiveExpression };
	}
	else if (*functionNamePtr == "fmap")
	{
		if (argumentsPtr->size() < 2)
		{
			fatal2("the function '%s' requires at least %zu arguments", "fmap", 2);
		}
		auto argument = argumentsPtr->front();
		argumentsPtr->erase(argumentsPtr->begin());

		auto variableName = getUniqueVariableName();
		auto bracedVariableName = format2("(%s)", variableName);

		vector< string > mappedCalls;
		for (const auto& e: *argumentsPtr) { mappedCalls.push_back(e + bracedVariableName); }
		string expression = format2("or(%s)", join(",", mappedCalls));

		*functionNamePtr = "with";
		*argumentsPtr = { variableName, argument, expression };
	}
}

void processAliases(string* functionNamePtr, vector< string >* argumentsPtr)
{
	{ // simple aliases
		static map< string, string > aliases = {
			{ "Pn", "package:name" },
			{ "Pi", "package:installed" },
			{ "Pai", "package:automatically-installed" },

			{ "v", "version" },
			{ "m", "maintainer" },
			{ "p", "priority" },
			{ "s", "section" },
			{ "t", "trusted" },
			{ "f", "field" },
			{ "sp", "source-package" },
			{ "sv", "source-version" },
			{ "e", "essential" },
			{ "i", "installed" },
			{ "d", "description" },
			{ "o", "provides" },
			{ "u", "uploaders" },

			{ "Ypd", "pre-depends" },
			{ "Yd", "depends" },
			{ "Yr", "recommends" },
			{ "Ys", "suggests" },
			{ "Ye", "enhances" },
			{ "Yc", "conflicts" },
			{ "Yb", "breaks" },
			{ "Yrp", "replaces" },

			{ "YRpd", "reverse-pre-depends" },
			{ "YRd", "reverse-depends" },
			{ "YRr", "reverse-recommends" },
			{ "YRs", "reverse-suggests" },
			{ "YRe", "reverse-enhances" },
			{ "YRc", "reverse-conflicts" },
			{ "YRb", "reverse-breaks" },
			{ "YRrp", "reverse-replaces" },

			{ "Zbd", "build-depends" },
			{ "Zbdi", "build-depends-indep" },
			{ "Zbc", "build-conflicts" },
			{ "Zbci", "build-conflicts-indep" },

			{ "ZRbd", "reverse-build-depends" },
			{ "ZRbdi", "reverse-build-depends-indep" },
			{ "ZRbc", "reverse-build-conflicts" },
			{ "ZRbci", "reverse-build-conflicts-indep" },

			{ "Ra", "release:archive" },
			{ "Rn", "release:codename" },
			{ "Rc", "release:component" },
			{ "Rv", "release:version" },
			{ "Ro", "release:vendor" },
			{ "Ru", "release:origin" },
		};
		auto it = aliases.find(*functionNamePtr);
		if (it != aliases.end())
		{
			*functionNamePtr = it->second;
			return;
		}
	}

	processNonTrivialAliases(functionNamePtr, argumentsPtr);
}

void parseFunctionNameAndArguments(const string& query,
		string* functionNamePtr, vector< string >* argumentsPtr)
{
	vector< string > possibleArguments;
	possibleArguments = split(query, '|');
	if (possibleArguments.size() > 1)
	{
		*functionNamePtr = "or";
		*argumentsPtr = std::move(possibleArguments);
		return;
	}
	possibleArguments = split(query, '&');
	if (possibleArguments.size() > 1)
	{
		*functionNamePtr = "and";
		*argumentsPtr = std::move(possibleArguments);
		return;
	}

	auto argumentsPosition = query.find_first_of("()");
	if (argumentsPosition == string::npos)
	{
		*functionNamePtr = query; // assume that the function takes no parameters
	}
	else
	{
		if (query[argumentsPosition] == ')')
		{
			fatal2(__("closing bracket ')' doesn't have a corresponding opening bracket '('"));
		}
		// now we know it's surely '('
		if (query.back() != ')')
		{
			fatal2(__("the last query character is not a closing bracket ')'"));
		}
		*functionNamePtr = query.substr(0, argumentsPosition);
		*argumentsPtr = split(query.substr(argumentsPosition + 1, query.size() - argumentsPosition - 2));
	}
}

unique_ptr< CommonFS > internalParseFunctionQuery(const string& query, bool binary)
{
	try
	{
		if (query.empty())
		{
			fatal2(__("query cannot be empty"));
		}

		string functionName;
		vector< string > arguments;
		parseFunctionNameAndArguments(query, &functionName, &arguments);

		for (string& argument: arguments)
		{
			trim(argument);
			stripArgumentQuotes(argument);
		}
		processAliases(&functionName, &arguments);
		return unique_ptr< CommonFS >(constructFSByName(functionName, arguments, binary));
	}
	catch (Exception&)
	{
		fatal2(__("unable to parse the query '%s'"), query);
		__builtin_unreachable();
	}
}

}

struct FunctionalSelector::Data
{
	Context context;
	VersionSetGetter binaryGetter;
	VersionSetGetter sourceGetter;

	Data(const Cache& cache_)
		: context(cache_), binaryGetter(cache_, true), sourceGetter(cache_, false)
	{}
};
FunctionalSelector::FunctionalSelector(const Cache& cache)
{
	__data = new Data(cache);
}
FunctionalSelector::~FunctionalSelector()
{
	delete __data;
}

unique_ptr< FS > FunctionalSelector::parseQuery(const string& query, bool binary)
{
	Cache::memoize = true;

	auto trimmedQuery = query;
	trim(trimmedQuery);
	auto result = internalParseFunctionQuery(trimmedQuery, binary);

	if (binary)
	{
		unique_ptr< CommonFS > newResult(new BinaryTagDummyFS(std::move(result)));
		result.swap(newResult);
	}
	return std::move(result);
}

list< SPCV > FunctionalSelector::selectAllVersions(const FS& functionSelector)
{
	const CommonFS* commonFS = dynamic_cast< const CommonFS* >(&functionSelector);
	if (!commonFS)
	{
		fatal2i("selectVersion: functionSelector is not an ancestor of CommonFS");
	}
	bool binary = (dynamic_cast< const BinaryTagDummyFS* >(commonFS));
	VersionSet versionSet(&__data->binaryGetter, &__data->sourceGetter);
	versionSet.selectGetterType(binary);
	return commonFS->select(__data->context, versionSet);
}

list< SPCV > FunctionalSelector::selectBestVersions(const FS& functionSelector)
{
	auto result = selectAllVersions(functionSelector);
	result.unique([](const SPCV& left, const SPCV& right) { return left->packageName == right->packageName; });
	return result;
}
