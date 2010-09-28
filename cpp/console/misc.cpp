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

#include <cstring>
#include <map>
using std::map;

#include <cupt/regex.hpp>
#include <cupt/download/progresses/console.hpp>

#include "common.hpp"
#include "misc.hpp"
#include "handlers.hpp"

string parseCommonOptions(int argc, char** argv, shared_ptr< Config > config, vector< string >& unparsed)
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
	vector< string> commandArguments;
	// parsing
	bpo::options_description options("Common options");
	vector< string > directOptions;
	string targetRelease;
	options.add_options()
		("important,i", "")
		("option,o", bpo::value< vector< string > >(&directOptions))
		("recurse", "")
		("purge", "")
		("all-versions,a", "")
		("no-all-versions", "")
		("target-release", bpo::value< string >(&targetRelease))
		("default-release,t", bpo::value< string >(&targetRelease))
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
				.positional(positionalOptions).allow_unregistered().run();
		bpo::store(parsed, variablesMap);
		unparsed = bpo::collect_unrecognized(parsed.options, bpo::exclude_positional);
		if (variablesMap.count("arguments"))
		{
			vector<std::string> arguments = variablesMap["arguments"].as< vector< string > >();
			unparsed.insert(unparsed.end(), arguments.begin(), arguments.end());
		}
		bpo::notify(variablesMap);

		{ // processing
			if (command.empty())
			{
				fatal("no command specified");
			}
			if (variablesMap.count("important"))
			{
				config->setScalar("apt::cache::important", "yes");
			}
			if (variablesMap.count("recurse"))
			{
				config->setScalar("apt::cache::recursedepends", "yes");
			}
			if (!targetRelease.empty())
			{
				config->setScalar("apt::default-release", targetRelease);
			}
			if (variablesMap.count("purge"))
			{
				config->setScalar("cupt::worker::purge", "yes");
			}
			if (variablesMap.count("all-versions"))
			{
				config->setScalar("apt::cache::allversions", "yes");
			}
			if (variablesMap.count("no-all-versions"))
			{
				config->setScalar("apt::cache::allversions", "no");
			}
			if (variablesMap.count("simulate"))
			{
				config->setScalar("cupt::worker::simulate", "yes");
			}
			if (variablesMap.count("quiet"))
			{
				config->setScalar("quiet", "yes");
				handleQuietOption(config);
			}
		}

		smatch m;
		FORIT(directOptionIt, directOptions)
		{
			static const sregex optionRegex = sregex::compile("(.*?)=(.*)");
			const string& directOption = *directOptionIt;
			if (!regex_match(directOption, m, optionRegex))
			{
				fatal("invalid option syntax in '%s' (right is '<option>=<value>')", directOption.c_str());
			}
			string key = m[1];
			string value = m[2];

			static const sregex listOptionNameRegex = sregex::compile("(.*?)::");
			if (regex_match(key, m, listOptionNameRegex))
			{
				// this is list option
				config->setList(m[1], value);
			}
			else
			{
				// regular option
				config->setScalar(key, value);
			}
		}
	}
	catch (const bpo::error& e)
	{
		fatal("failed to parse command-line options: %s", e.what());
	}
	catch (Exception&)
	{
		fatal("error while processing command-line options");
	}
	return command;
}

bpo::variables_map parseOptions(const Context& context, bpo::options_description options,
		vector< string >& arguments)
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
				.options(all).positional(positionalOptions).run();
		bpo::store(parsed, variablesMap);
	}
	catch (const bpo::unknown_option& e)
	{
		fatal("unknown option '%s'", e.get_option_name().c_str());
	}
	catch (const bpo::error& e)
	{
		fatal("failed to parse options: %s", e.what());
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
		{ "satisfy", [](Context& c) -> int { return managePackages(c, ManagePackages::Satisfy); } },
		{ "build-dep", [](Context& c) -> int { return managePackages(c, ManagePackages::BuildDepends); } },
		{ "dist-upgrade", &distUpgrade },
		{ "update", &updateReleaseAndIndexData },
		{ "shell", &shell },
		{ "source", &downloadSourcePackage },
		{ "markauto", [](Context& c) -> int { return changeAutoInstalledState(c, true); } },
		{ "unmarkauto", [](Context& c) -> int { return changeAutoInstalledState(c, false); } },
		{ "clean", [](Context& c) -> int { return cleanArchives(c, false); } },
		{ "autoclean", [](Context& c) -> int { return cleanArchives(c, true); } },
		{ "changelog", [](Context& c) -> int { return downloadChangelogOrCopyright(c, ChangelogOrCopyright::Changelog); } },
		{ "copyright", [](Context& c) -> int { return downloadChangelogOrCopyright(c, ChangelogOrCopyright::Copyright); } },
		{ "screenshots", &showScreenshotUris },
	};
	/* TODO: snapshot handler
		'snapshot' => \&snapshot,
	*/
	auto it = handlerMap.find(command);
	if (it == handlerMap.end())
	{
		fatal("unrecognized command '%s'", command.c_str());
	}
	return it->second;
}

void checkNoExtraArguments(const vector< string >& arguments)
{
	if (!arguments.empty())
	{
		auto argumentsString = join(" ", arguments);
		warn("extra arguments '%s' are not processed", argumentsString.c_str());
	}
}

void handleQuietOption(const shared_ptr< Config >& config)
{
	if (config->getBool("quiet"))
	{
		if (!freopen("/dev/null", "w", stdout))
		{
			fatal("unable to redirect standard output to '/dev/null': EEE");
		}
	}
}

shared_ptr< Progress > getDownloadProgress(const shared_ptr< const Config >& config)
{
	return shared_ptr< Progress >(config->getBool("quiet") ? new Progress : new ConsoleProgress);
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
			fatal("error while loading config");
		}
	}
	return __config;
}

shared_ptr< const Cache > Context::getCache(
		bool useSource, bool useBinary, bool useInstalled,
		const vector< string >& packageNameGlobsToReinstall)
{
	bool needsRebuild =
			!__cache ||
			!packageNameGlobsToReinstall.empty() ||
			(useSource && !__used_source) ||
			((useBinary != __used_binary || useInstalled != __used_installed) && (useBinary || useInstalled));

	if (needsRebuild)
	{
		try
		{
			__cache.reset(new Cache(__config, useSource, useBinary, useInstalled, packageNameGlobsToReinstall));
		}
		catch (Exception&)
		{
			fatal("error while creating package cache");
		}
	}

	__used_source = useSource;
	__used_binary = useBinary;
	__used_installed = useInstalled;

	return __cache;
}

void Context::clearCache()
{
	__cache.reset();
}

