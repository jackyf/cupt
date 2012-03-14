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
using std::cin;

#include <dlfcn.h>
#include <readline/readline.h>
#include <readline/history.h>

#include <cupt/file.hpp>
#include <cupt/cache/package.hpp>

#include "../common.hpp"
#include "../cupt.hpp"
#include "../handlers.hpp"

bool shellMode = false;

class Readline
{
	string __prompt;
	static char* (*__dl_readline)(const char*);
	static void (*__dl_add_history)(const char*);

	string __gnu_readline()
	{
		char* buffer = __dl_readline(__prompt.c_str());

		if (!buffer)
		{
			return string();
		}
		string result(buffer);
		free(buffer);

		if (!result.empty())
		{
			__dl_add_history(result.c_str());
		}

		return result;
	}

	string __simple_get_line()
	{
		string result;
		cout << __prompt;
		std::getline(cin, result);
		return result;
	}
 public:
	Readline(const string& prompt)
		: __prompt(prompt)
	{}

	string getLine()
	{
		string result;

		start:

		result = (__dl_readline && __dl_add_history) ? __gnu_readline() : __simple_get_line();

		if (result.empty())
		{
			goto start;
		}
		else if (result == "exit" || result == "quit" || result == ":q" || result == "q")
		{
			result.clear();
		}

		return result;
	}

	static void init()
	{
		auto handle = dlopen("libreadline.so.6", RTLD_NOW);
		if (!handle)
		{
			warn2(__("unable to dynamically find libreadline.so.6: dlopen: %s"), dlerror());
			return;
		}

		__dl_readline = reinterpret_cast< decltype(__dl_readline) >(dlsym(handle, "readline"));
		if (!__dl_readline)
		{
			warn2(__("unable to dynamically bind the symbol '%s': %s"), "readline", dlerror());
		}

		__dl_add_history = reinterpret_cast< decltype(__dl_add_history) >(dlsym(handle, "add_history"));
		if (!__dl_add_history)
		{
			warn2(__("unable to dynamically bind the symbol '%s': %s"), "add_history", dlerror());
		}
	}
};

char* (*Readline::__dl_readline)(const char*) = NULL;
void (*Readline::__dl_add_history)(const char*) = NULL;

void convertLineToArgcArgv(const string& line, int& argc, char**& argv)
{
	vector< string > arguments;

	// kind of hack to get arguments as it was real shell
	// if you know easier way, let me know :)
	string errorString;
	// 'A' - to not let echo interpret $word as an option
	string shellCommand = format2("(for word in %s; do echo A$word; done)", line);
	File pipe(shellCommand, "pr", errorString);
	if (!errorString.empty())
	{
		fatal2(__("unable to open an internal shell pipe: %s"), errorString);
	}

	string argument;
	while (!pipe.getLine(argument).eof())
	{
		arguments.push_back(argument.substr(1));
	}

	argc = arguments.size() + 1;
	argv = new char*[argc];
	argv[0] = strdup("cupt-shell");
	for (int i = 1; i < argc; ++i)
	{
		argv[i] = strdup(arguments[i-1].c_str());
	}
}

void freeArgcArgv(int argc, char** argv)
{
	for (int i = 0; i < argc; ++i)
	{
		free(argv[i]);
	}
	delete [] argv;
}

int shell(Context& context)
{
	shellMode = true;
	Package::memoize = true;

	vector< string > arguments;
	bpo::options_description noOptions;
	parseOptions(context, noOptions, arguments);
	checkNoExtraArguments(arguments);

	Readline::init();

	cout << __("This is an interactive shell of the cupt package manager.\n");

	const shared_ptr< Config > oldConfig(new Config(*(context.getConfig())));

	Readline term(/* prompt */ "cupt> ");
	string line;
	while (line = term.getLine(), !line.empty())
	{
		int argc;
		char** argv;

		convertLineToArgcArgv(line, argc, argv);

		string command;
		{
			mainEx(argc, argv, context, command);
		};

		static const vector< string > safeCommands = { "config-dump", "show", "showsrc",
				"search", "depends", "rdepends", "policy", "policysrc", "pkgnames", "changelog",
				"copyright", "screenshots", "source", "clean", "autoclean" };
		if (std::find(safeCommands.begin(), safeCommands.end(), command) == safeCommands.end())
		{
			// the system could be modified, need to rebuild all
			context.clearCache();
		}
		*(context.getConfig()) = *oldConfig;
		freeArgcArgv(argc, argv);
	}

	return 0;
}

