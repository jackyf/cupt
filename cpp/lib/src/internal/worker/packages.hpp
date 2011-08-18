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
#ifndef CUPT_INTERNAL_WORKER_PACKAGES_SEEN
#define CUPT_INTERNAL_WORKER_PACKAGES_SEEN

#include <list>

#include <cupt/download/manager.hpp>

#include <internal/graph.hpp>

#include <internal/worker/base.hpp>

namespace cupt {
namespace internal {

using std::list;

class VersionProxy
{
 protected:
	shared_ptr< const BinaryVersion > _version;
 public:
	const string& getPackageName() const
	{
		return _version->packageName;
	}
	const string& getVersionString() const
	{
		return _version->versionString;
	}

	virtual const RelationLine& getRelations(BinaryVersion::RelationTypes::Type) const = 0;
	virtual const shared_ptr< const BinaryVersion >& getVersion() const = 0;
	virtual string toString() const = 0;
	virtual const string& getAdditionaSortKey() const = 0;
	virtual ~VersionProxy() {}
};
class FullVersionProxy: public VersionProxy
{
 public:
	const RelationLine& getRelations(BinaryVersion::RelationTypes::Type type) const
	{
		return _version->relations[type];
	}
	void setVersion(const shared_ptr< const BinaryVersion >& version)
	{
		_version = version;
	}
	const shared_ptr< const BinaryVersion >& getVersion() const
	{
		return _version;
	}
	string toString() const
	{
		return getPackageName() + ' ' + getVersionString();
	}
	const string& getAdditionaSortKey() const
	{
		static const string emptyString;
		return emptyString;
	}
};
class OneRelationExpressionVersionProxy: public VersionProxy
{
	BinaryVersion::RelationTypes::Type __type;
	RelationLine __relation_expression;
	string __hash_key;

	static const RelationLine __null_result;
 public:
	OneRelationExpressionVersionProxy(const shared_ptr< const BinaryVersion >& version,
			BinaryVersion::RelationTypes::Type type, const RelationExpression& relationExpression)
		: __type(type), __hash_key(relationExpression.getHashString())
	{
		_version = version;
		__relation_expression.push_back(relationExpression);
	}
	const RelationLine& getRelations(BinaryVersion::RelationTypes::Type type) const
	{
		return (type == __type) ? __relation_expression : __null_result;
	}
	const shared_ptr< const BinaryVersion >& getVersion() const
	{
		fatal("internal error: getting version of one relation expression proxy");
		return _version; // unreachable
	}
	string toString() const
	{
		return getPackageName() + " [" + __relation_expression[0].toString()
				+ "] " + getVersionString();
	}
	const string& getAdditionaSortKey() const
	{
		return __hash_key;
	}
};

struct InnerAction
{
	enum Type { PriorityModifier, Remove, Unpack, Configure } type;
	shared_ptr< const VersionProxy > versionProxy;
	bool fake;
	mutable const InnerAction* linkedFrom;
	mutable const InnerAction* linkedTo;
	mutable ssize_t priority;

	InnerAction();
	bool operator<(const InnerAction& other) const;
	string toString() const;
};
struct InnerActionGroup: public vector< InnerAction >
{
	string dpkgFlags;
	bool continued;

	InnerActionGroup() : continued(false) {}
};
struct GraphAndAttributes
{
	Graph< InnerAction > graph;
	struct RelationInfoRecord
	{
		BinaryVersion::RelationTypes::Type dependencyType;
		RelationExpression relationExpression;
		bool reverse;
	};
	struct Attribute
	{
		bool isFundamental;
		vector< RelationInfoRecord > relationInfo;

		Attribute();
		bool isDependencyHard() const;
		bool isFromVirtual() const;
	};
	map< pair< const InnerAction*, const InnerAction* >, Attribute > attributes;
};
struct Changeset
{
	vector< InnerActionGroup > actionGroups;
	vector< pair< download::Manager::DownloadEntity, string > > downloads;
};

class PackagesWorker: public virtual WorkerBase
{
	std::set< string > __auto_installed_package_names;

	void __fill_actions(GraphAndAttributes&,
			vector< pair< const InnerAction*, const InnerAction* > >&);
	bool __build_actions_graph(GraphAndAttributes&);
	map< string, pair< download::Manager::DownloadEntity, string > > __prepare_downloads();
	vector< Changeset > __get_changesets(GraphAndAttributes&,
			const map< string, pair< download::Manager::DownloadEntity, string > >&);
	void __run_dpkg_command(const string&, const string&, const string&);
	void __do_dpkg_pre_actions();
	void __do_dpkg_post_actions();
	string __generate_input_for_preinstall_v2_hooks(const vector< InnerActionGroup >&);
	void __do_dpkg_pre_packages_actions(const vector< InnerActionGroup >&);
	void __clean_downloads(const Changeset& changeset);
	void __do_downloads(const vector< pair< download::Manager::DownloadEntity, string > >&,
			const shared_ptr< download::Progress >&);
	static void __check_graph_pre_depends(GraphAndAttributes& gaa, bool);
	void __change_auto_status(const InnerActionGroup&);
 public:
	PackagesWorker();

	void markAsAutomaticallyInstalled(const string& packageName, bool targetStatus);
	void changeSystem(const shared_ptr< download::Progress >&);
};

}
}

#endif

