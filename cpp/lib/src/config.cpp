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
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
using std::map;


#include <boost/lexical_cast.hpp>

#include <cupt/common.hpp>
#include <cupt/config.hpp>
#include <cupt/file.hpp>

#include <internal/common.hpp>
#include <internal/configparser.hpp>
#include <internal/filesystem.hpp>
#include <internal/regex.hpp>

namespace cupt {

namespace internal {

class ConfigImpl
{
	vector< sregex > __optionalPatterns;
	void __initOptionalPatterns();
 public:
	map< string, string > regularVars;
	map< string, string > regularCompatibilityVars;
	map< string, vector< string > > listVars;

	void initializeVariables();
	vector< string > getConfigurationFilePaths(Config*) const;
	string getEnvBasedConfigurationFilePath(const Config*, const char*, const char*) const;
	void parsePreConfiguration(const Config*, ConfigParser&);
	void parseConfigurationFile(ConfigParser&, const string&);
	void readConfigs(Config*);
	void setArchitecture(Config*);
	bool isOptionalOption(const string& optionName) const;
};

void ConfigImpl::__initOptionalPatterns()
{
	const char* optionalPatterns[] = {
		// used APT vars
		"acquire::*::*::proxy",
		"acquire::*::proxy::*",
		"acquire::*::proxy",
		"acquire::*::*::dl-limit",
		"acquire::*::dl-limit::*",
		"acquire::*::dl-limit",
		"acquire::*::*::timeout",
		"acquire::*::timeout::*",
		"acquire::*::timeout",
		"dpkg::tools::options::*",
		"dpkg::tools::options::*::*",

		// used Cupt vars
		"cupt::downloader::protocols::*::priority",
		"cupt::downloader::protocols::*::methods",
		"cupt::downloader::protocols::*::methods::*::priority",
	};

	const sregex convertRegex = sregex::compile("\\*");
	for (const auto& pattern: optionalPatterns)
	{
		auto currentRegexString = regex_replace(string(pattern), convertRegex, "[^:]*?");
		__optionalPatterns.emplace_back(sregex::compile(currentRegexString));
	}
}

void ConfigImpl::initializeVariables()
{
	regularVars =
	{
		// used APT vars
		{ "acquire::http::timeout", "120" },
		{ "acquire::http::allowredirect", "yes" },
		{ "acquire::https::timeout", "120" },
		{ "acquire::ftp::timeout", "120" },
		{ "acquire::file::timeout", "20" },
		{ "acquire::retries", "0" },
		{ "apt::acquire::max-default-age::debian-security", "7" },
		{ "apt::acquire::translation", "environment" },
		{ "apt::architecture", "" }, // will be set a bit later
		{ "apt::authentication::trustcdrom", "no" },
		{ "apt::cache::allversions", "no" },
		{ "apt::cache::important", "no" },
		{ "apt::cache::namesonly", "no" },
		{ "apt::cache::recursedepends", "no" },
		{ "apt::default-release", "" },
		{ "apt::install-recommends", "yes" },
		{ "apt::install-suggests", "no" },
		{ "apt::get::allowunauthenticated", "no" },
		{ "apt::get::list-cleanup", "yes" },
		{ "dir", "/" },
		{ "dir::bin::dpkg", "/usr/bin/dpkg" },
		{ "dir::cache", "var/cache/apt" },
		{ "dir::cache::archives", "archives" },
		{ "dir::etc", "etc/apt" },
		{ "dir::etc::sourcelist", "sources.list" },
		{ "dir::etc::sourceparts", "sources.list.d" },
		{ "dir::etc::parts", "apt.conf.d" },
		{ "dir::etc::main", "apt.conf" },
		{ "dir::etc::preferences", "preferences" },
		{ "dir::etc::preferencesparts", "preferences.d" },
		{ "dir::state", "var/lib/apt" },
		{ "dir::state::extendedstates", "extended_states" },
		{ "dir::state::lists", "lists" },
		{ "dir::state::status", "/var/lib/dpkg/status" },
		{ "gpgv::trustedkeyring", "/var/lib/cupt/trusted.gpg" },
		{ "quiet", "0" }, // bool, '0' instead of 'no' for apt-listchanges (#604130)

		// Cupt vars
		{ "cupt::cache::limit-releases::by-archive::type", "none" },
		{ "cupt::cache::limit-releases::by-codename::type", "none" },
		{ "cupt::cache::pin::addendums::downgrade", "-10000" },
		{ "cupt::cache::pin::addendums::hold", "1000000" },
		{ "cupt::cache::pin::addendums::not-automatic", "-4000" },
		{ "cupt::cache::pin::addendums::but-automatic-upgrades", "4200" },
		{ "cupt::cache::release-file-expiration::ignore", "no" },
		{ "cupt::console::allow-untrusted", "no" },
		{ "cupt::console::assume-yes", "no" },
		{ "cupt::console::actions-preview::show-archives", "no" },
		{ "cupt::console::actions-preview::show-codenames", "no" },
		{ "cupt::console::actions-preview::show-components", "no" },
		{ "cupt::console::actions-preview::show-not-preferred", "for-upgrades" },
		{ "cupt::console::actions-preview::show-details", "yes" },
		{ "cupt::console::actions-preview::show-reasons", "no" },
		{ "cupt::console::actions-preview::show-size-changes", "no" },
		{ "cupt::console::actions-preview::show-summary", "yes" },
		{ "cupt::console::actions-preview::show-vendors", "no" },
		{ "cupt::console::actions-preview::show-versions", "no" },
		{ "cupt::console::show-progress-messages", "yes" },
		{ "cupt::console::use-colors", "no" },
		{ "cupt::directory", "/" },
		{ "cupt::directory::configuration", "etc/cupt" },
		{ "cupt::directory::configuration::main", "cupt.conf" },
		{ "cupt::directory::configuration::main-parts", "cupt.conf.d" },
		{ "cupt::directory::configuration::pre", "pre.conf" },
		{ "cupt::directory::log", "var/log/cupt.log" },
		{ "cupt::directory::state", "var/lib/cupt" },
		{ "cupt::directory::state::lists", "lists" },
		{ "cupt::directory::state::snapshots", "snapshots" },
		{ "cupt::downloader::max-simultaneous-downloads", "2" },
		{ "cupt::downloader::protocols::file::priority", "300" },
		{ "cupt::downloader::protocols::copy::priority", "250" },
		{ "cupt::downloader::protocols::debdelta::priority", "150" },
		{ "cupt::downloader::protocols::https::priority", "125" },
		{ "cupt::downloader::protocols::http::priority", "100" },
		{ "cupt::downloader::protocols::ftp::priority", "80" },
		{ "cupt::downloader::protocols::file::methods::file::priority", "100" },
		{ "cupt::downloader::protocols::copy::methods::file::priority", "100" },
		{ "cupt::downloader::protocols::debdelta::methods::debdelta::priority", "100" },
		{ "cupt::downloader::protocols::https::methods::curl::priority", "100" },
		{ "cupt::downloader::protocols::http::methods::curl::priority", "100" },
		{ "cupt::downloader::protocols::ftp::methods::curl::priority", "100" },
		{ "cupt::downloader::protocols::https::methods::wget::priority", "80" },
		{ "cupt::downloader::protocols::http::methods::wget::priority", "80" },
		{ "cupt::downloader::protocols::ftp::methods::wget::priority", "80" },
		{ "cupt::languages::indexes", "environment,en" },
		{ "cupt::update::check-release-files", "yes" },
		{ "cupt::update::compression-types::gz::priority", "100" },
		{ "cupt::update::compression-types::bz2::priority", "100" },
		{ "cupt::update::compression-types::lzma::priority", "100" },
		{ "cupt::update::compression-types::xz::priority", "100" },
		{ "cupt::update::compression-types::uncompressed::priority", "100" },
		{ "cupt::update::generate-index-of-index", "yes" },
		{ "cupt::update::use-index-diffs", "yes" },
		{ "cupt::resolver::auto-remove", "yes" },
		{ "cupt::resolver::external-command", "" },
		{ "cupt::resolver::keep-recommends", "yes" },
		{ "cupt::resolver::keep-suggests", "no" },
		{ "cupt::resolver::max-solution-count", "8192" },
		{ "cupt::resolver::no-remove", "no" },
		{ "cupt::resolver::synchronize-by-source-versions", "none" },
		{ "cupt::resolver::track-reasons", "no" },
		{ "cupt::resolver::type", "fair" },
		{ "cupt::resolver::score::new", "-5" },
		{ "cupt::resolver::score::removal", "-1800" },
		{ "cupt::resolver::score::removal-of-autoinstalled", "1200" },
		{ "cupt::resolver::score::removal-of-essential", "-200000" },
		{ "cupt::resolver::score::upgrade", "100" },
		{ "cupt::resolver::score::downgrade", "-800" },
		{ "cupt::resolver::score::position-penalty", "-2" },
		{ "cupt::resolver::score::quality-adjustment", "40" },
		{ "cupt::resolver::score::unsatisfied-recommends", "-240" },
		{ "cupt::resolver::score::unsatisfied-suggests", "-60" },
		{ "cupt::resolver::score::unsatisfied-try", "-10000" },
		{ "cupt::resolver::score::unsatisfied-wish", "-500" },
		{ "cupt::resolver::score::failed-synchronization", "-80" },
		{ "cupt::worker::archives-space-limit", "0" },
		{ "cupt::worker::defer-triggers", "auto" },
		{ "cupt::worker::download-only", "no" },
		{ "cupt::worker::log", "yes" },
		{ "cupt::worker::log::levels::metadata", "1" },
		{ "cupt::worker::log::levels::packages", "2" },
		{ "cupt::worker::log::levels::snapshots", "1" },
		{ "cupt::worker::purge", "no" },
		{ "cupt::worker::simulate", "no" },
		{ "cupt::worker::use-locks", "yes" },
		{ "debug::downloader", "no" },
		{ "debug::logger", "no" },
		{ "debug::resolver", "no" },
		{ "debug::worker", "no" },
		{ "debug::gpgv", "no" },
	};

	regularCompatibilityVars =
	{
		{ "apt::get::allowunauthenticated", "cupt::console::allow-untrusted" },
		{ "apt::get::assume-yes", "cupt::console::assume-yes" },
		{ "apt::get::automaticremove", "cupt::resolver::auto-remove" },
		{ "apt::get::purge", "cupt::worker::purge" },
	};

	listVars =
	{
		// used APT vars
		{ "apt::neverautoremove", vector< string > {} },
		{ "apt::update::pre-invoke", vector< string > {} },
		{ "apt::update::post-invoke", vector< string > {} },
		{ "apt::update::post-invoke-success", vector< string > {} },
		{ "dir::ignore-files-silently", { "~$", "\\.disabled$", "\\.bak$", "\\.dpkg-[a-z]+$" } },
		{ "dpkg::options", vector< string > {} },
		{ "dpkg::pre-install-pkgs", vector< string > {} },
		{ "dpkg::pre-invoke", vector< string > {} },
		{ "dpkg::post-invoke", vector< string > {} },

		// Cupt vars
		{ "cupt::cache::foreign-architectures", {} },
		{ "cupt::cache::limit-releases::by-archive", vector< string > {} },
		{ "cupt::cache::limit-releases::by-codename", vector< string > {} },
		{ "cupt::downloader::protocols::file::methods", vector< string > { "file" } },
		{ "cupt::downloader::protocols::copy::methods", vector< string > { "file" } },
		{ "cupt::downloader::protocols::debdelta::methods", vector< string > { "debdelta" } },
		{ "cupt::downloader::protocols::https::methods", vector< string > { "curl", "wget" } },
		{ "cupt::downloader::protocols::http::methods", vector< string > { "curl", "wget" } },
		{ "cupt::downloader::protocols::ftp::methods", vector< string > { "curl", "wget" } },
		{ "cupt::resolver::no-autoremove-if-rdepends-exist", {} },
	};

	__initOptionalPatterns();
}

namespace {

bool matchesAnyOfRegexes(const string& s, const vector< sregex >& regexes)
{
	smatch m;
	return std::any_of(regexes.begin(), regexes.end(),
			[&m, &s](const sregex& regex)
			{
				return regex_match(s, m, regex);
			});
}

vector< string > getConfigurationPartsFilePaths(
		const string& partsDirectoryPath, const vector< sregex >& ignorePathRegexes)
{
	using namespace std::placeholders;

	vector< string > result = internal::fs::glob(partsDirectoryPath + "/*");
	result.erase(std::remove_if(result.begin(), result.end(),
			std::bind(matchesAnyOfRegexes, _1, ignorePathRegexes)), result.end());
	return result;
}

}

bool ConfigImpl::isOptionalOption(const string& optionName) const
{
	return matchesAnyOfRegexes(optionName, __optionalPatterns);
}

vector< string > ConfigImpl::getConfigurationFilePaths(Config* config) const
{
	vector< sregex > ignorePathRegexes;
	for (const auto& ignorePathRegexString: config->getList("dir::ignore-files-silently"))
	{
		auto fullString = "^.*" + ignorePathRegexString + ".*$";
		ignorePathRegexes.emplace_back(stringToRegex(fullString));
	}

	vector< string > result;
	{ // APT files
		result = getConfigurationPartsFilePaths(config->getPath("dir::etc::parts"), ignorePathRegexes);

		auto mainFilePath = getEnvBasedConfigurationFilePath(config, "APT_CONFIG", "dir::etc::main");
		if (!mainFilePath.empty())
		{
			result.push_back(mainFilePath);
		}
	}
	{ // Cupt files
		auto cuptParts = getConfigurationPartsFilePaths(
				config->getPath("cupt::directory::configuration::main-parts"), ignorePathRegexes);
		result.insert(result.end(), cuptParts.begin(), cuptParts.end());
		auto mainFilePath = config->getPath("cupt::directory::configuration::main");
		if (internal::fs::fileExists(mainFilePath))
		{
			result.push_back(mainFilePath);
		}
	}
	return result;
}

string ConfigImpl::getEnvBasedConfigurationFilePath(const Config* config, const char* env, const char* optionName) const
{
	string path = config->getPath(optionName);
	const char* envPath = getenv(env);
	if (envPath)
	{
		path = envPath;
	}

	return internal::fs::fileExists(path) ? path : string();
}

void ConfigImpl::readConfigs(Config* config)
{
	static auto unquoteValue = [](const string& value) -> string
	{
		if (value.size() < 2)
		{
			fatal2i("unquoted simple value '%s'", value);
		}
		return string(value.begin() + 1, value.end() - 1);
	};

	static auto regularHandler = [&config](const string& name, const string& value)
	{
		config->setScalar(name, unquoteValue(value));
	};
	static auto listHandler = [&config](const string& name, const string& value)
	{
		config->setList(name, unquoteValue(value));
	};
	static auto clearHandler = [this](const string& name, const string& /* no value */)
	{
		const sregex nameRegex = sregex::compile(name);
		smatch m;
		FORIT(it, this->regularVars)
		{
			if (regex_search(it->first, m, nameRegex, regex_constants::match_continuous))
			{
				it->second.clear();
			}
		}
		FORIT(it, this->listVars)
		{
			if (regex_search(it->first, m, nameRegex, regex_constants::match_continuous))
			{
				it->second.clear();
			}
		}
	};

	internal::ConfigParser parser(regularHandler, listHandler, clearHandler);
	{
		parsePreConfiguration(config, parser);

		for (const auto& path: getConfigurationFilePaths(config))
		{
			parseConfigurationFile(parser, path);
		}
	}
}

void ConfigImpl::parsePreConfiguration(const Config* config, ConfigParser& parser)
{
	auto path = getEnvBasedConfigurationFilePath(config, "CUPT_PRE_CONFIG", "cupt::directory::configuration::pre");
	if (!path.empty())
	{
		parseConfigurationFile(parser, path);
	}
}

void ConfigImpl::parseConfigurationFile(ConfigParser& parser, const string& path)
{
	try
	{
		parser.parse(path);
	}
	catch (Exception&)
	{
		warn2(__("skipped the configuration file '%s'"), path);
	}
}

static string qx(const string& shellCommand)
{
	string openError;
	File file(shellCommand, "pr", openError); // reading from pipe
	if (!openError.empty())
	{
		fatal2(__("unable to open the pipe '%s': %s"), shellCommand, openError);
	}
	string result;
	file.getFile(result);
	return result;
}

void ConfigImpl::setArchitecture(Config* config)
{
	const string dpkgPath = config->getPath("dir::bin::dpkg");
	string architecture = qx(dpkgPath + " --print-architecture");
	internal::chomp(architecture);
	config->setScalar("apt::architecture", architecture);

	string foreignArchitectures = qx(dpkgPath + " --print-foreign-architectures");
	for (const auto& item: internal::split('\n', foreignArchitectures))
	{
		config->setList("cupt::cache::foreign-architectures", item);
	}
}

}

Config::Config()
{
	__impl = new internal::ConfigImpl;
	__impl->initializeVariables();
	__impl->readConfigs(this);
	__impl->setArchitecture(this);
}

Config::~Config()
{
	delete __impl;
}

Config::Config(const Config& other)
{
	__impl = new internal::ConfigImpl(*other.__impl);
}

Config& Config::operator=(const Config& other)
{
	if (this == &other)
	{
		return *this;
	}
	delete __impl;
	__impl = new internal::ConfigImpl(*other.__impl);
	return *this;
}

vector< string > Config::getScalarOptionNames() const
{
	vector< string > result;
	FORIT(regularVariableIt, __impl->regularVars)
	{
		result.push_back(regularVariableIt->first);
	}
	return result;
}

vector< string > Config::getListOptionNames() const
{
	vector< string > result;
	FORIT(listVariableIt, __impl->listVars)
	{
		result.push_back(listVariableIt->first);
	}
	return result;
}

string Config::getString(const string& optionName) const
{
	auto it = __impl->regularVars.find(optionName);
	if (it != __impl->regularVars.cend())
	{
		return it->second; // found
	}
	else if (__impl->isOptionalOption(optionName))
	{
		return "";
	}
	else
	{
		fatal2(__("an attempt to get the invalid scalar option '%s'"), optionName);
	}
	__builtin_unreachable();
}

string Config::getPath(const string& optionName) const
{
	auto shallowResult = getString(optionName);
	if (!shallowResult.empty() && shallowResult[0] != '/')
	{
		// relative path -> combine with prefix

		// let's see if we have a prefix
		auto doubleColonPosition = optionName.rfind("::");
		if (doubleColonPosition != string::npos)
		{
			auto prefixOptionName = optionName.substr(0, doubleColonPosition);
			// let's see is it defined
			if (__impl->regularVars.find(prefixOptionName) != __impl->regularVars.cend())
			{
				return getPath(prefixOptionName) + '/' + shallowResult;
			}
		}
	}
	return shallowResult;
}

bool Config::getBool(const string& optionName) const
{
	auto result = getString(optionName);
	if (result.empty() || result == "false" || result == "0" || result == "no")
	{
		return false;
	}
	else
	{
		return true;
	}
}

ssize_t Config::getInteger(const string& optionName) const
{
	auto source = getString(optionName);
	if (source.empty())
	{
		return 0;
	}
	else
	{
		ssize_t result = 0;
		try
		{
			result = boost::lexical_cast< ssize_t >(source);
		}
		catch (boost::bad_lexical_cast&)
		{
			fatal2(__("unable to convert '%s' to a number"), source);
		}
		return result; // we'll never return default value here
	}
}

vector< string > Config::getList(const string& optionName) const
{
	auto it = __impl->listVars.find(optionName);
	if (it != __impl->listVars.end())
	{
		return it->second;
	}
	else if (__impl->isOptionalOption(optionName))
	{
		return vector< string >();
	}
	else
	{
		fatal2(__("an attempt to get the invalid list option '%s'"), optionName);
	}
	__builtin_unreachable();
}

static bool isFamilyOption(const string& optionName, const char* family)
{
	string prefix = string(family) + "::";
	return optionName.compare(0, prefix.size(), prefix) == 0;
}
static inline bool isCuptOption(const string& optionName)
{
	return isFamilyOption(optionName, "cupt");
}
static bool isForeignOption(const string& optionName)
{
	return !isCuptOption(optionName) && !isFamilyOption(optionName, "dpkg");
}

void Config::setScalar(const string& optionName, const string& value)
{
	string normalizedOptionName = optionName;
	FORIT(charIt, normalizedOptionName)
	{
		*charIt = std::tolower(*charIt);
	}

	{ // translation to cupt variable names
		auto it = __impl->regularCompatibilityVars.find(normalizedOptionName);
		if (it != __impl->regularCompatibilityVars.end())
		{
			// setting the value for old variable
			__impl->regularVars[normalizedOptionName] = value;

			normalizedOptionName = it->second;
		}
	}

	if (isForeignOption(normalizedOptionName))
	{
		__impl->regularVars[optionName /* <-- non-normalized one */] = value;
	}
	else if (__impl->regularVars.count(normalizedOptionName) || __impl->isOptionalOption(normalizedOptionName))
	{
		__impl->regularVars[normalizedOptionName] = value;
	}
	else
	{
		if (isCuptOption(optionName))
		{
			warn2(__("an attempt to set the invalid scalar option '%s'"), optionName);
		}
	}
}

void Config::setList(const string& optionName, const string& value)
{
	string normalizedOptionName = optionName;
	FORIT(charIt, normalizedOptionName)
	{
		*charIt = std::tolower(*charIt);
	}

	if (__impl->listVars.count(normalizedOptionName) || __impl->isOptionalOption(normalizedOptionName))
	{
		__impl->listVars[normalizedOptionName].push_back(value);
	}
	else
	{
		if (isCuptOption(optionName))
		{
			warn2(__("an attempt to set the invalid list option '%s'"), optionName);
		}
	}
}

} // namespace

