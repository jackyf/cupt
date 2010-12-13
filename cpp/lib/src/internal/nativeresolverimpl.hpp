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
#ifndef CUPT_INTERNAL_NATIVERESOLVERIMPL_SEEN
#define CUPT_INTERNAL_NATIVERESOLVERIMPL_SEEN

#include <set>
#include <list>

#include <cupt/fwd.hpp>
#include <cupt/system/resolver.hpp>

#include <internal/solution.hpp>

namespace cupt {
namespace internal {

using namespace cache;
using std::list;
using std::unique_ptr;

class NativeResolverImpl
{
 public:
	typedef list< shared_ptr< Solution > >::iterator SolutionListIterator;
 private:
	typedef Resolver::Reason Reason;
	typedef Resolver::UserReason UserReason;
	typedef Resolver::AutoRemovalReason AutoRemovalReason;
	typedef Resolver::SynchronizationReason SynchronizationReason;
	typedef Resolver::RelationExpressionReason RelationExpressionReason;

	typedef std::function< SolutionListIterator (list< shared_ptr< Solution > >&) > SolutionChooser;

	struct Action
	{
		string packageName;
		shared_ptr< const BinaryVersion > version;
		vector< string > packageToStickNames;
		unique_ptr< const RelationExpression > fakelySatisfies;
		shared_ptr< const Reason > reason;
		float profit;

		Action() : profit(NAN) {};
	};

	shared_ptr< const Config > __config;
	shared_ptr< const Cache > __cache;
	set< string > __manually_modified_package_names;
	SolutionStorage __solution_storage;
	shared_ptr< Solution > __old_solution;
	shared_ptr< Solution > __initial_solution;
	RelationLine __satisfy_relation_expressions;
	RelationLine __unsatisfy_relation_expressions;

	struct InstallVersionResult
	{
		enum Type { Ok, Restricted, Unsynchronizeable };
	};

	void __import_installed_versions();
	vector< DependencyEntry > __get_dependency_groups() const;
	InstallVersionResult::Type __prepare_version_no_stick(const shared_ptr< const BinaryVersion >&,
			const shared_ptr< const Reason >&, PackageEntry&);
	float __get_version_weight(const shared_ptr< const BinaryVersion >&) const;
	float __get_action_profit(const shared_ptr< const BinaryVersion >&,
			const shared_ptr< const BinaryVersion >&) const;
	bool __can_package_be_removed(const string& packageName) const;
	void __clean_automatically_installed(const shared_ptr< Solution >&);
	SolutionChooser __select_solution_chooser() const;
	void __require_strict_relation_expressions();
	void __pre_apply_action(const Solution&, Solution&, unique_ptr< Action > &&);
	void __calculate_profits(const shared_ptr< Solution >&, vector< unique_ptr< Action > >& actions) const;
	void __pre_apply_actions_to_solution_tree(list< shared_ptr< Solution > >& solutions,
			const shared_ptr< Solution >&, vector< unique_ptr< Action > >&);
	void __erase_worst_solutions(list< shared_ptr< Solution > >& solutions);
	void __post_apply_action(Solution&);
	void __add_actions_to_modify_package_entry(vector< unique_ptr< Action > >&, const string&,
			const PackageEntry&, BinaryVersion::RelationTypes::Type, const RelationExpression&,
			const vector< shared_ptr< const BinaryVersion > >&, bool tryHard = false);
	void __add_actions_to_fix_dependency(vector< unique_ptr< Action > >&, const shared_ptr< Solution >&,
			const vector< shared_ptr< const BinaryVersion > >&);
	void __prepare_stick_requests(vector< unique_ptr< Action > >& actions) const;
	Resolver::UserAnswer::Type __propose_solution(
			const Solution&, Resolver::CallbackType);
	bool __is_soft_dependency_ignored(const shared_ptr< const BinaryVersion >&,
			BinaryVersion::RelationTypes::Type, const RelationExpression&,
			const vector< shared_ptr< const BinaryVersion > >&) const;
	vector< string > __get_unsynchronizeable_related_package_names(const Solution&,
			const shared_ptr< const BinaryVersion >&);
	bool __can_related_packages_be_synchronized(
			const Solution&, const shared_ptr< const BinaryVersion >&);
	void __synchronize_related_packages(Solution&,
			const shared_ptr< const BinaryVersion >&, bool);
	void __filter_unsynchronizeable_actions(
			const Solution&, vector< unique_ptr< Action > >&);

	struct BrokenDependencyInfo
	{
		const RelationExpression* relationExpressionPtr;
		const shared_ptr< const BinaryVersion >* intersectVersionPtr;
		vector< shared_ptr< const BinaryVersion > > satisfyingVersions;
	};

	bool __verify_relation_line(const shared_ptr< Solution >&,
			const string* packageNamePtr, const PackageEntry&,
			BinaryVersion::RelationTypes::Type, bool isDependencyAnti,
			BrokenDependencyInfo*);
	void __generate_possible_actions(vector< unique_ptr< Action > >*, const shared_ptr< Solution >&,
			const string& packageName, const PackageEntry&, const BrokenDependencyInfo&,
			BinaryVersion::RelationTypes::Type, bool isDependencyAnti);

	static const string __dummy_package_name;
 public:
	NativeResolverImpl(const shared_ptr< const Config >&, const shared_ptr< const Cache >&);

	void installVersion(const shared_ptr< const BinaryVersion >&);
	void satisfyRelationExpression(const RelationExpression&);
	void unsatisfyRelationExpression(const RelationExpression&);
	void removePackage(const string& packageName);
	void upgrade();

	bool resolve(Resolver::CallbackType);
};

}
}

#endif

