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

#include <cstring>
#include <map>
using std::map;
#include <sstream>

#include <common/regex.hpp>

#include <cupt/file.hpp>
#include <cupt/download/progresses/console.hpp>

#include "common.hpp"
#include "misc.hpp"
#include "handlers.hpp"

void parseReleaseLimit(Config& config, const string& limitName, const string& included, const string& excluded)
{
	auto setLimitList = [&config](const string& listOptionName, const string& valuesString)
	{
		std::istringstream valueStream(valuesString);
		string value;
		while (std::getline(valueStream, value, ','))
		{
			config.setList(listOptionName, value);
		}
	};

	auto limitOptionName = string("cupt::cache::limit-releases::by-") + limitName;
	if (!included.empty() && !excluded.empty())
	{
		fatal2(__("options '--include-%ss' and '--exclude-%ss' cannot be specified together"),
				limitName, limitName);
	}
	else if (!included.empty())
	{
		config.setScalar(limitOptionName + "::type", "include");
		setLimitList(limitOptionName, included);
	}
	else if (!excluded.empty())
	{
		config.setScalar(limitOptionName + "::type", "exclude");
		setLimitList(limitOptionName, excluded);
	}
}

void parseReleaseLimits(Config& config, const string& includedArchives, const string& excludedArchives,
		const string& includedCodenames, const string& excludedCodenames)
{
	parseReleaseLimit(config, "archive", includedArchives, excludedArchives);
	parseReleaseLimit(config, "codename", includedCodenames, excludedCodenames);
}

void handleQuietOption(const Config&);

string parseCommonOptions(int argc, char** argv, Config& config, vector< string >& unparsed)
{
	if (argc == 3)
	{
		if (!strcmp(argv[1], "get") && !strcmp(argv[2], "ride"))
		{
			printf("You've got ride\n");
			exit(0);
		}
	}

	string command;
	// parsing
	bpo::options_description options("Common options");
	vector< string > directOptions;
	string targetRelease;
	string includedArchives, excludedArchives, includedCodenames, excludedCodenames;
	options.add_options()
		("important,i", "")
		("option,o", bpo::value< vector< string > >(&directOptions))
		("recurse", "")
		("all-versions,a", "")
		("no-all-versions", "")
		("target-release", bpo::value< string >(&targetRelease))
		("default-release,t", bpo::value< string >(&targetRelease))
		("include-archives", bpo::value< string >(&includedArchives))
		("exclude-archives", bpo::value< string >(&excludedArchives))
		("include-codenames", bpo::value< string >(&includedCodenames))
		("exclude-codenames", bpo::value< string >(&excludedCodenames))
		("simulate,s", "")
		("quiet,q", "")
		("command", bpo::value< string >(&command))
		("arguments", bpo::value< vector< string > >());

	bpo::positional_options_description positionalOptions;
	positionalOptions.add("command", 1);
	positionalOptions.add("arguments", -1);

	try
	{
		bpo::variables_map variablesMap;
		bpo::parsed_options parsed = bpo::command_line_parser(argc, argv).options(options)
				.style(bpo::command_line_style::default_style & ~bpo::command_line_style::allow_guessing)
				.positional(positionalOptions).allow_unregistered().run();
		bpo::store(parsed, variablesMap);
		bpo::notify(variablesMap);

		{ // do not pass 'command' further
			auto commandOptionIt = std::find_if(parsed.options.begin(), parsed.options.end(),
					[](const bpo::option& o) { return o.string_key == "command"; });
			if (commandOptionIt != parsed.options.end())
			{
				parsed.options.erase(commandOptionIt);
			}
		}
		unparsed = bpo::collect_unrecognized(parsed.options, bpo::include_positional);

		{ // processing
			if (command.empty())
			{
				fatal2(__("no command specified"));
			}
			if (variablesMap.count("important"))
			{
				config.setScalar("apt::cache::important", "yes");
			}
			if (variablesMap.count("recurse"))
			{
				config.setScalar("apt::cache::recursedepends", "yes");
			}
			if (!targetRelease.empty())
			{
				config.setScalar("apt::default-release", targetRelease);
			}
			if (variablesMap.count("all-versions"))
			{
				config.setScalar("apt::cache::allversions", "yes");
			}
			if (variablesMap.count("no-all-versions"))
			{
				config.setScalar("apt::cache::allversions", "no");
			}
			if (variablesMap.count("simulate"))
			{
				config.setScalar("cupt::worker::simulate", "yes");
			}
			if (variablesMap.count("quiet"))
			{
				config.setScalar("quiet", "yes");
				handleQuietOption(config);
			}
			parseReleaseLimits(config, includedArchives, excludedArchives,
					includedCodenames, excludedCodenames);
		}

		smatch m;
		for (const string& directOption: directOptions)
		{
			static const sregex optionRegex = sregex::compile("(.*?)=(.*)");
			if (!regex_match(directOption, m, optionRegex))
			{
				fatal2(__("invalid option syntax in '%s' (right is '<option>=<value>')"), directOption);
			}
			string key = m[1];
			string value = m[2];

			static const sregex listOptionNameRegex = sregex::compile("(.*?)::");
			if (regex_match(key, m, listOptionNameRegex))
			{
				// this is list option
				config.setList(m[1], value);
			}
			else
			{
				// regular option
				config.setScalar(key, value);
			}
		}
	}
	catch (const bpo::error& e)
	{
		fatal2(__("failed to parse command-line options: %s"), e.what());
	}
	catch (Exception&)
	{
		fatal2(__("error while processing command-line options"));
	}
	return command;
}

bpo::variables_map parseOptions(const Context& context, bpo::options_description options,
		vector< string >& arguments, std::function< pair< string, string > (const string&) > extraParser)
{
	bpo::options_description argumentOptions("");
	argumentOptions.add_options()
		("arguments", bpo::value< vector< string > >(&arguments));

	bpo::options_description all("");
	all.add(options);
	all.add(argumentOptions);

	bpo::positional_options_description positionalOptions;
	positionalOptions.add("arguments", -1);

	bpo::variables_map variablesMap;
	try
	{
		bpo::parsed_options parsed = bpo::command_line_parser(context.unparsed)
				.style(bpo::command_line_style::default_style & ~bpo::command_line_style::allow_guessing)
				.options(all).positional(positionalOptions).extra_parser(extraParser).run();
		bpo::store(parsed, variablesMap);
	}
	catch (const bpo::unknown_option& e)
	{
		fatal2(__("unknown option '%s'"), e.get_option_name());
	}
	catch (const bpo::error& e)
	{
		fatal2(__("failed to parse command-line options: %s"), e.what());
	}
	bpo::notify(variablesMap);

	return variablesMap;
}

std::function< int (Context&) > getHandler(const string& command)
{
	static map< string, std::function< int (Context&) > > handlerMap = {
		{ "search", &search },
		{ "show", &showBinaryVersions },
		{ "showsrc", &showSourceVersions },
		{ "depends", [](Context& c) -> int { return showRelations(c, false); } },
		{ "rdepends", [](Context& c) -> int { return showRelations(c, true); } },
		{ "policy", [](Context& c) -> int { return policy(c, false); } },
		{ "policysrc", [](Context& c) -> int { return policy(c, true); } },
		{ "config-dump", &dumpConfig },
		{ "pkgnames", &showPackageNames },
		{ "why", &findDependencyChain },
		{ "install", [](Context& c) -> int { return managePackages(c, ManagePackages::Install); } },
		{ "remove", [](Context& c) -> int { return managePackages(c, ManagePackages::Remove); } },
		{ "purge", [](Context& c) -> int { return managePackages(c, ManagePackages::Purge); } },
		{ "safe-upgrade", [](Context& c) -> int { return managePackages(c, ManagePackages::SafeUpgrade); } },
		{ "full-upgrade", [](Context& c) -> int { return managePackages(c, ManagePackages::FullUpgrade); } },
		{ "reinstall", [](Context& c) -> int { return managePackages(c, ManagePackages::Reinstall); } },
		{ "iii", [](Context& c) -> int { return managePackages(c, ManagePackages::InstallIfInstalled); } },
		{ "satisfy", [](Context& c) -> int { return managePackages(c, ManagePackages::Satisfy); } },
		{ "build-dep", [](Context& c) -> int { return managePackages(c, ManagePackages::BuildDepends); } },
		{ "dist-upgrade", &distUpgrade },
		{ "update", &updateReleaseAndIndexData },
		{ "shell", &shell },
		{ "source", &downloadSourcePackage },
		{ "markauto", [](Context& c) -> int { return managePackages(c, ManagePackages::Markauto); } },
		{ "unmarkauto", [](Context& c) -> int { return managePackages(c, ManagePackages::Unmarkauto); } },
		{ "showauto", &showAutoInstalled },
		{ "clean", [](Context& c) -> int { return cleanArchives(c, false); } },
		{ "autoclean", [](Context& c) -> int { return cleanArchives(c, true); } },
		{ "changelog", [](Context& c) -> int { return downloadChangelogOrCopyright(c, ChangelogOrCopyright::Changelog); } },
		{ "copyright", [](Context& c) -> int { return downloadChangelogOrCopyright(c, ChangelogOrCopyright::Copyright); } },
		{ "screenshots", &showScreenshotUris },
		{ "snapshot", &snapshot },
		{ "tar-metadata", &tarMetadata },
	};
	auto it = handlerMap.find(command);
	if (it == handlerMap.end())
	{
		fatal2(__("unrecognized command '%s'"), command);
	}
	return it->second;
}

void checkNoExtraArguments(const vector< string >& arguments)
{
	if (!arguments.empty())
	{
		auto argumentsString = join(" ", arguments);
		warn2(__("extra arguments '%s' are not processed"), argumentsString);
	}
}

vector< string > convertLineToShellArguments(const string& line)
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

	return arguments;
}

void handleQuietOption(const Config& config)
{
	if (config.getBool("quiet"))
	{
		if (!freopen("/dev/null", "w", stdout))
		{
			fatal2e(__("unable to redirect standard output to '/dev/null'"));
		}
	}
}

shared_ptr< Progress > getDownloadProgress(const Config& config)
{
	return shared_ptr< Progress >(config.getBool("quiet") ? new Progress : new ConsoleProgress);
}

shared_ptr< Config > Context::getConfig()
{
	if (!__config)
	{
		try
		{
			__config.reset(new Config);
		}
		catch (Exception&)
		{
			fatal2(__("error while loading the configuration"));
		}
	}
	return __config;
}

Context::Context()
	: __used_source(false), __used_binary(false), __used_installed(false),
	__valid(true)
{}

shared_ptr< const Cache > Context::getCache(
		bool useSource, bool useBinary, bool useInstalled)
{
	bool needsRebuild =
			!__cache ||
			!__valid ||
			(useSource && !__used_source) ||
			((useBinary != __used_binary || useInstalled != __used_installed) && (useBinary || useInstalled));

	if (needsRebuild)
	{
		try
		{
			__cache.reset(new Cache(__config, useSource, useBinary, useInstalled));
		}
		catch (Exception&)
		{
			fatal2(__("error while creating the package cache"));
		}
	}

	__used_source = useSource;
	__used_binary = useBinary;
	__used_installed = useInstalled;
	__valid = true;

	return __cache;
}

void Context::clearCache()
{
	__cache.reset();
}

void Context::invalidate()
{
	__valid = false;
}


