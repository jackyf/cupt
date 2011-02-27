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

namespace cupt {
namespace internal {

using namespace cache;
using std::list;
using std::unique_ptr;
using std::set;

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
		const dg::Element* oldElementPtr; // may be NULL
		const dg::Element* newElementPtr; // many not be NULL
		shared_ptr< const Reason > reason;
		ScoreChange profit;
	};

	shared_ptr< const Config > __config;
	shared_ptr< const Cache > __cache;
	set< string > __manually_modified_package_names;
	SolutionStorage __solution_storage;
	ScoreManager __score_manager;

	map< string, shared_ptr< const BinaryVersion > > __old_packages;
	map< string, dg::InitialPackageEntry > __initial_packages;
	RelationLine __satisfy_relation_expressions;
	RelationLine __unsatisfy_relation_expressions;

	void __import_installed_versions();
	bool __prepare_version_no_stick(const shared_ptr< const BinaryVersion >&,
			dg::InitialPackageEntry&);
	float __get_version_weight(const shared_ptr< const BinaryVersion >&) const;
	float __get_action_profit(const shared_ptr< const BinaryVersion >&,
			const shared_ptr< const BinaryVersion >&) const;
	bool __is_candidate_for_auto_removal(const dg::Element*,
		const std::function< bool (const string&) >, bool);
	void __clean_automatically_installed(Solution&);
	SolutionChooser __select_solution_chooser() const;
	void __require_strict_relation_expressions();
	void __pre_apply_action(const Solution&, Solution&, unique_ptr< Action > &&);
	void __calculate_profits(vector< unique_ptr< Action > >& actions) const;
	void __pre_apply_actions_to_solution_tree(list< shared_ptr< Solution > >& solutions,
			const shared_ptr< Solution >&, vector< unique_ptr< Action > >&);

	void __initial_validate_pass(Solution&);
	void __validate_element(Solution&, const dg::Element*);
	void __validate_changed_package(Solution&, const dg::Element*, const dg::Element*);
	void __post_apply_action(Solution&);
	void __final_verify_solution(const Solution&);

	bool __makes_sense_to_modify_package(const Solution&, const dg::Element*,
			const dg::Element*, bool);
	void __add_actions_to_modify_package_entry(vector< unique_ptr< Action > >&, const Solution&,
			const dg::Element*, const dg::Element*, bool);

	void __add_actions_to_fix_dependency(vector< unique_ptr< Action > >&, const Solution&,
			const dg::Element*);
	Resolver::UserAnswer::Type __propose_solution(
			const Solution&, Resolver::CallbackType, bool);

	bool __verify_element(const Solution&, const dg::Element*);
	void __generate_possible_actions(vector< unique_ptr< Action > >*, const Solution&,
			const dg::Element*, const dg::Element*, bool);

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

