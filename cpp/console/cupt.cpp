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
#include <clocale>
#include <iostream>
using std::cout;
using std::endl;
#include <map>

#include "cupt.hpp"
#include "misc.hpp"

void showOwnVersion();
void showHelp(const char*);

int main(int argc, char* argv[])
{
	setlocale(LC_ALL, "");
	cupt::messageFd = STDERR_FILENO;

	if (argc > 1)
	{
		if (!strcmp(argv[1], "version") || !strcmp(argv[1], "--version") || !strcmp(argv[1], "-v"))
		{
			if (argc > 2)
			{
				warn2(__("the command '%s' doesn't accept arguments"), argv[1]);
			}
			showOwnVersion();
			return 0;
		}
		if (!strcmp(argv[1], "help") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))
		{
			if (argc > 2)
			{
				warn2(__("the command '%s' doesn't accept arguments"), argv[1]);
			}
			showHelp(argv[0]);
			return 0;
		}
	}
	else
	{
		showHelp(argv[0]);
		return 0;
	}

	Context context;
	return mainEx(argc, argv, context);
}

int mainEx(int argc, char* argv[], Context& context)
{
	try
	{
		auto command = parseCommonOptions(argc, argv, /* in */ *context.getConfig(),
				/* out */ context.unparsed);
		context.argc = argc;
		context.argv = argv;
		std::function< int (Context&) > handler = getHandler(command);
		try
		{
			return handler(context);
		}
		catch (Exception&)
		{
			fatal2(__("error performing the command '%s'"), command);
		}
	}
	catch (Exception&)
	{
		return 1;
	}
	return 255; // we should not reach it, and gcc issues internal error on __builtin_unreachable
}

void showOwnVersion()
{
	#define QUOTED(x) QUOTED_(x)
	#define QUOTED_(x) # x
	cout << "executable: " << QUOTED(CUPT_VERSION) << endl;
	#undef QUOTED
	#undef QUOTED_
	cout << "library: " << cupt::libraryVersion << endl;
}

void showHelp(const char* argv0)
{
	using std::map;
	map< string, string > actionDescriptions = {
		{ "help", __("prints a short help") },
		{ "version", __("prints versions of this program and the underlying library") },
		{ "config-dump", __("prints values of configuration variables") },
		{ "show", __("prints the info about binary package(s)") },
		{ "showsrc", __("prints the info about source packages(s)") },
		{ "search", __("searches for packages using regular expression(s)") },
		{ "depends", __("prints dependencies of binary package(s)") },
		{ "rdepends", __("print reverse-dependencies of binary package(s)") },
		{ "why", __("finds a dependency path between a package set and a package") },
		{ "policy", __("prints the pin info for the binary package(s)") },
		{ "policysrc", __("prints the pin info for the source package(s)") },
		{ "pkgnames", __("prints available package names") },
		{ "changelog", __("views the Debian changelog(s) of binary package(s)") },
		{ "copyright", __("views the Debian copyright(s) info of binary package(s)") },
		{ "screenshots", __("views Debian screenshot web pages for binary package(s)") },
		{ "update", __("updates repository metadata") },
		{ "install", __("installs/upgrades/downgrades binary package(s)") },
		{ "reinstall", __("reinstalls binary packages(s)") },
		{ "remove", __("removes binary package(s)") },
		{ "purge", __("removes binary package(s) along with their configuration files") },
		{ "iii", __("\"install if installed\": upgrades/downgrades binary packages(s)") },
		{ "satisfy", __("performs actions to make relation expressions satisfied") },
		{ "safe-upgrade", __("upgrades the system without removing non-automatically installed packages") },
		{ "full-upgrade", __("upgrades the system") },
		{ "dist-upgrade", __("does a two-stage full upgrade") },
		{ "build-dep", __("satisfies build dependencies for source package(s)") },
		{ "source", __("fetches and unpacks source package(s)") },
		{ "clean", __("cleans the whole binary package cache") },
		{ "autoclean", __("cleans packages from the binary package cache if not available from repositories") },
		{ "markauto", __("marks binary package(s) as automatically installed") },
		{ "unmarkauto", __("marks binary package(s) as manually installed") },
		{ "showauto", __("shows the list of manually or automatically installed packages") },
		{ "shell", __("starts an interactive package manager shell") },
		{ "snapshot", __("works with system snapshots") },
	};

	cout << format2(__("Usage: %s <action> [<parameters>]"), argv0) << endl;
	cout << endl;
	cout << __("Actions:") << endl;
	for (const auto& pair: actionDescriptions)
	{
		cout << "  " << pair.first << ": " << pair.second << endl;
	}
}
