/**************************************************************************
*   Copyright (C) 2013 by Eugene V. Lyubimkin                             *
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
#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/versionstring.hpp>

#include <internal/worker/dpkg.hpp>

namespace cupt {
namespace internal {

namespace {

string getFullBinaryCommand(const Config& config)
{
	string dpkgBinary = config.getPath("dir::bin::dpkg");

	for (const string& option: config.getList("dpkg::options"))
	{
		dpkgBinary += " ";
		dpkgBinary += option;
	}

	return dpkgBinary;
}

string getActionName(InnerAction::Type actionType,
		const Worker::ActionsPreview& actionsPreview, const InnerActionGroup& actionGroup)
{
	string result;

	switch (actionType)
	{
		case InnerAction::Remove:
		{
			const string& packageName = actionGroup.rbegin()->version->packageName;
			result = actionsPreview.groups[Worker::Action::Purge].count(packageName) ?
					"purge" : "remove";
		}
			break;
		case InnerAction::Unpack:
			result = "unpack";
			break;
		case InnerAction::Configure:
			if (actionGroup.size() >= 2 && (actionGroup.rbegin() + 1)->type == InnerAction::Unpack)
			{
				result = "install"; // [remove+]unpack+configure
			}
			else
			{
				result = "configure";
			}
			break;
	}

	return result;
}

void debugActionCommand(const InnerActionGroup& actionGroup, const string& requestedDpkgOptions)
{
	vector< string > stringifiedActions;
	for (const auto& action: actionGroup)
	{
		stringifiedActions.push_back(action.toString());
	}
	auto actionsString = join(" & ", stringifiedActions);
	debug2("do: (%s) %s%s", actionsString, requestedDpkgOptions,
			actionGroup.continued ? " (continued)" : "");
}

bool shouldDeferTriggers(const Config& config)
{
	const string& optionName = "cupt::worker::defer-triggers";
	if (config.getString(optionName) == "auto")
	{
		return true;
	}
	else
	{
		return config.getBool(optionName);
	}
}

string getActionLog(const InnerActionGroup& actionGroup,
		InnerAction::Type actionType, const string& actionName)
{
	vector< string > subResults;
	for (const auto& action: actionGroup)
	{
		if (action.type == actionType)
		{
			subResults.push_back(actionName + ' ' + action.version->packageName
					+ ' ' + action.version->versionString);
		}
	}
	return join(" & ", subResults);
}

}

string Dpkg::p_getActionCommand(const string& requestedDpkgOptions,
		InnerAction::Type actionType, const string& actionName,
		const InnerActionGroup& actionGroup)
{
	auto dpkgCommand = p_fullCommand + " --" + actionName;
	if (p_shouldDeferTriggers)
	{
		dpkgCommand += " --no-triggers";
	}
	dpkgCommand += requestedDpkgOptions;

	/* the workaround for a dpkg bug #558151

	   dpkg performs some IMHO useless checks for programs in PATH
	   which breaks some upgrades of packages that contains important programs

	   It is possible that this hack is not needed anymore with
	   better scheduling heuristics of 2.x but we cannot
	   re-evaluate it with lenny->squeeze (e)glibc upgrade since
	   new Cupt requires new libgcc which in turn requires new
	   glibc.
	*/
	dpkgCommand += " --force-bad-path";

	for (const auto& action: actionGroup)
	{
		if (action.type != actionType)
		{
			continue; // will be true for non-last linked actions
		}
		string actionExpression;
		if (actionName == "unpack" || actionName == "install")
		{
			actionExpression = p_archivesDirectoryPath + '/' +
					p_base->_get_archive_basename(action.version);
		}
		else
		{
			actionExpression = action.version->packageName;
		}
		dpkgCommand += " ";
		dpkgCommand += actionExpression;
	}

	return dpkgCommand;
}

void Dpkg::p_makeSureThatSystemIsTriggerClean()
{
	p_base->_logger->log(Logger::Subsystem::Packages, 2, "running all package triggers");

	auto command = p_fullCommand + " --triggers-only -a";
	p_base->_run_external_command(Logger::Subsystem::Packages, command);
}

Dpkg::Dpkg(WorkerBase* wb) :
	p_base(wb),
	p_fullCommand(getFullBinaryCommand(*wb->_config)),
	p_shouldDeferTriggers(shouldDeferTriggers(*wb->_config)),
	p_archivesDirectoryPath(wb->_get_archives_directory()),
	p_debugging(wb->_config->getBool("debug::worker"))
{
	p_makeSureThatSystemIsTriggerClean();
}

void Dpkg::p_runPendingTriggers()
{
	p_base->_logger->log(Logger::Subsystem::Packages, 2, "running pending triggers");

	string command = p_fullCommand + " --triggers-only --pending";
	p_base->_run_external_command(Logger::Subsystem::Packages, command);
}

Dpkg::~Dpkg()
{
	// triggers might be not processed due to '--no-triggers' or dependency problems (see #766758)
	p_runPendingTriggers();
}

void Dpkg::doActionGroup(const InnerActionGroup& actionGroup, const Worker::ActionsPreview& actionsPreview)
{
	auto actionType = actionGroup.getCompoundActionType();
	string actionName = getActionName(actionType, actionsPreview, actionGroup);

	string requestedDpkgOptions;
	for (const auto& dpkgFlag: actionGroup.dpkgFlags)
	{
		requestedDpkgOptions += string(" ") + dpkgFlag;
	}

	auto dpkgCommand = p_getActionCommand(requestedDpkgOptions, actionType, actionName, actionGroup);
	if (p_debugging) debugActionCommand(actionGroup, requestedDpkgOptions);
	p_base->_logger->log(Logger::Subsystem::Packages, 2, getActionLog(actionGroup, actionType, actionName));
	p_base->_run_external_command(Logger::Subsystem::Packages, dpkgCommand);
}

}
}
