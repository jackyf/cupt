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

#include <internal/nativeresolver/solution.hpp>
#include <internal/nativeresolver/score.hpp>
#include <internal/nativeresolver/decisionfailtree.hpp>
#include <internal/nativeresolver/autoremovalpossibility.hpp>

namespace cupt {
namespace internal {

using namespace cache;
using std::list;
using std::unique_ptr;
using std::set;

class NativeResolverImpl
{
	typedef Resolver::Reason Reason;
	typedef Resolver::UserReason UserReason;
	typedef Resolver::AutoRemovalReason AutoRemovalReason;
	typedef Resolver::SynchronizationReason SynchronizationReason;
	typedef Resolver::RelationExpressionReason RelationExpressionReason;
	typedef Solution::Action Action;

	shared_ptr< const Config > __config;
	shared_ptr< const Cache > __cache;
	map< string, bool > __auto_status_overrides;
	unique_ptr< SolutionStorage > __solution_storage;
	ScoreManager __score_manager;
	AutoRemovalPossibility __auto_removal_possibility;

	map< string, const BinaryVersion* > __old_packages;
	map< string, dg::InitialPackageEntry > __initial_packages;
	RelationLine __satisfy_relation_expressions;
	RelationLine __unsatisfy_relation_expressions;
	BinaryVersion __custom_relations_version;

	DecisionFailTree __decision_fail_tree;
	bool __any_solution_was_found;

	void __import_installed_versions();
	void __import_packages_to_reinstall();
	bool __prepare_version_no_stick(const BinaryVersion*,
			dg::InitialPackageEntry&);
	float __get_version_weight(const BinaryVersion*) const;
	float __get_action_profit(const BinaryVersion*, const BinaryVersion*) const;

	bool __compute_target_auto_status(const string&) const;
	AutoRemovalPossibility::Allow __is_candidate_for_auto_removal(const dg::Element*);
	bool __clean_automatically_installed(Solution&);

	void __require_strict_relation_expressions();
	void __pre_apply_action(const Solution&, Solution&, unique_ptr< Action > &&, size_t);
	void __calculate_profits(vector< unique_ptr< Action > >& actions) const;
	void __pre_apply_actions_to_solution_tree(
			std::function< void (const shared_ptr< Solution >&) > callback,
			const shared_ptr< Solution >&, vector< unique_ptr< Action > >&);

	void __post_apply_action(Solution&);
	void __final_verify_solution(const Solution&);

	bool __makes_sense_to_modify_package(const Solution&, const dg::Element*,
			const dg::Element*, bool);
	void __add_actions_to_modify_package_entry(vector< unique_ptr< Action > >&, const Solution&,
			const dg::Element*, const dg::Element*, bool);

	void __add_actions_to_fix_dependency(vector< unique_ptr< Action > >&, const Solution&,
			const dg::Element*);
	void __prepare_reject_requests(vector< unique_ptr< Action > >& actions) const;
	void __fillSuggestedPackageReasons(const Solution&, const string&,
			Resolver::SuggestedPackage&, const dg::Element*, map< const dg::Element*, size_t >&) const;
	Resolver::UserAnswer::Type __propose_solution(
			const Solution&, Resolver::CallbackType, bool);

	void __generate_possible_actions(vector< unique_ptr< Action > >*, const Solution&,
			const dg::Element*, const dg::Element*, bool);

	static const string __dummy_package_name;
 public:
	NativeResolverImpl(const shared_ptr< const Config >&, const shared_ptr< const Cache >&);

	void installVersion(const BinaryVersion*);
	void satisfyRelationExpression(const RelationExpression&);
	void unsatisfyRelationExpression(const RelationExpression&);
	void removePackage(const string& packageName);
	void upgrade();
	void setAutomaticallyInstalledFlag(const string& packageName, bool flagValue);

	bool resolve(Resolver::CallbackType);
};

}
}

#endif

