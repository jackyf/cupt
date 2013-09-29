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
#include <iostream>
using std::cout;
using std::endl;

#include <cupt/system/snapshots.hpp>
#include <cupt/system/worker.hpp>

#include "../handlers.hpp"

int snapshot(Context& context)
{
	if (context.unparsed.size() >= 2 && context.unparsed[0] == "load")
	{
		// handling 'snapshot load' subcommand
		context.unparsed.erase(context.unparsed.begin()); // cutting 'load'
		return managePackages(context, ManagePackages::LoadSnapshot);
	}

	vector< string > arguments;
	bpo::options_description noOptions;
	auto variables = parseOptions(context, noOptions, arguments);
	if (arguments.empty())
	{
		fatal2(__("the action is not specified"));
	}
	string action = arguments[0];
	arguments.erase(arguments.begin());

	auto config = context.getConfig();
	Snapshots snapshots(config);

	if (action == "list")
	{
		checkNoExtraArguments(arguments);
		auto snapshotNames = snapshots.getSnapshotNames();
		for (const auto& name: snapshotNames)
		{
			cout << name << endl;
		}
	}
	else if (action == "save" || action == "remove")
	{
		if (arguments.empty())
		{
			fatal2(__("no snapshot name specified"));
		}
		string snapshotName = arguments[0];

		arguments.erase(arguments.begin());
		checkNoExtraArguments(arguments);

		if (action == "save")
		{
			auto cache = context.getCache(false, false, true);
			Worker worker(config, cache);
			worker.saveSnapshot(snapshots, snapshotName);
		}
		else // remove
		{
			auto cache = context.getCache(false, false, false);
			Worker worker(config, cache);
			worker.removeSnapshot(snapshots, snapshotName);
		}
	}
	else if (action == "rename")
	{
		if (arguments.empty())
		{
			fatal2(__("no previous snapshot name specified"));
		}
		string oldSnapshotName = arguments[0];
		if (arguments.size() < 2)
		{
			fatal2(__("no new snapshot name specified"));
		}
		string newSnapshotName = arguments[1];

		arguments.erase(arguments.begin(), arguments.begin() + 2);
		checkNoExtraArguments(arguments);

		auto cache = context.getCache(false, false, false);
		Worker worker(config, cache);
		worker.renameSnapshot(snapshots, oldSnapshotName, newSnapshotName);
	}
	else
	{
		fatal2(__("unsupported action '%s'"), action);
	}

	return 0;
}

