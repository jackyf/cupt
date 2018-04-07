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

struct BrokenPair;

class NativeResolverImpl
{
	typedef Resolver::Reason Reason;
	typedef Resolver::UserReason UserReason;
	typedef Resolver::AutoRemovalReason AutoRemovalReason;
	typedef Resolver::SynchronizationReason SynchronizationReason;
	typedef Resolver::RelationExpressionReason RelationExpressionReason;
	typedef Solution::Action Action;
	typedef vector< unique_ptr< Action > > ActionContainer;

	shared_ptr< const Config > __config;
	bool p_debugging;
	size_t p_maxLeafCount;
	shared_ptr< const Cache > __cache;
	map< string, bool > __auto_status_overrides;
	unique_ptr< SolutionStorage > __solution_storage;
	ScoreManager __score_manager;
	AutoRemovalPossibility __auto_removal_possibility;

	map< string, const BinaryVersion* > __old_packages;

	vector< dg::UserRelationExpression > p_userRelationExpressions;

	DecisionFailTree __decision_fail_tree;
	bool __any_solution_was_found;

	void __import_installed_versions();
	void __import_packages_to_reinstall();
	float __get_version_weight(const BinaryVersion*) const;
	float __get_action_profit(const BinaryVersion*, const BinaryVersion*) const;

	bool p_computeTargetAutoStatus(const string&, const PreparedSolution&, dg::Element) const;
	AutoRemovalPossibility::Allow p_isCandidateForAutoRemoval(const PreparedSolution&, dg::Element);
	bool __clean_automatically_installed(PreparedSolution&);

	void __pre_apply_action(const Solution&, Solution&, unique_ptr< Action > &&, size_t, size_t);
	ScoreChange p_getScoreChange(dg::Element, dg::Element, size_t) const;
	void __pre_apply_actions_to_solution_tree(
			std::function< void (const shared_ptr< Solution >&) > callback,
			const shared_ptr< PreparedSolution >&, vector< unique_ptr< Action > >&);

	void __final_verify_solution(const PreparedSolution&);

	bool __makes_sense_to_modify_package(const PreparedSolution&, dg::Element, dg::Element);
	void __add_actions_to_modify_package_entry(vector< unique_ptr< Action > >&, const PreparedSolution&,
			dg::Element, dg::Element);

	void __add_actions_to_fix_dependency(vector< unique_ptr< Action > >&, const PreparedSolution&, dg::Element);
	void __prepare_reject_requests(vector< unique_ptr< Action > >& actions) const;
	void __fillSuggestedPackageReasons(const PreparedSolution&, Resolver::SuggestedPackage&, dg::VersionElement) const;
	Resolver::UserAnswer::Type __propose_solution(
			const PreparedSolution&, Resolver::CallbackType, bool);

	void __fill_and_process_introduced_by(const PreparedSolution&, const BrokenPair&, ActionContainer* actionsPtr);
	void __generate_possible_actions(vector< unique_ptr< Action > >*, const PreparedSolution&, const BrokenPair&);

	enum class Resolve2Result { Yes, No, HitSolutionTreeLimit };
	Resolve2Result p_resolve2(const shared_ptr<PreparedSolution>&, Resolver::CallbackType);

 public:
	NativeResolverImpl(const shared_ptr< const Config >&, const shared_ptr< const Cache >&);

	void satisfyRelationExpression(const RelationExpression&, bool, const string&, RequestImportance, bool);
	void upgrade();
	void setAutomaticallyInstalledFlag(const string& packageName, bool flagValue);

	bool resolve(Resolver::CallbackType);
};

}
}

#endif

