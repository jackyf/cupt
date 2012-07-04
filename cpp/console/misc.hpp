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
#ifndef MISC_SEEN
#define MISC_SEEN

#include "common.hpp"

#include <functional>

#include <boost/program_options.hpp>
namespace bpo = boost::program_options;

class Context
{
	shared_ptr< Config > __config;
	shared_ptr< Cache > __cache;
	bool __used_source;
	bool __used_binary;
	bool __used_installed;
	bool __valid;
 public:
	Context();

	shared_ptr< Config > getConfig();
	shared_ptr< const Cache > getCache(
			bool useSource, bool useBinary, bool useInstalled,
			const vector< string >& packageNameGlobsToReinstall = vector< string >());
	void clearCache();
	void invalidate();

	vector< string > unparsed;
	int argc; // argc, argv - for exec() in distUpgrade()
	char* const* argv;
};
std::function< int (Context&) > getHandler(const string&);

string parseCommonOptions(int argc, char** argv, Config&, vector< string >& unparsed);
bpo::variables_map parseOptions(const Context& context, bpo::options_description options,
		vector< string >& arguments,
		std::function< pair< string, string > (const string&) > extraParser =
		[](const string&) -> pair< string, string > { return make_pair(string(), string()); } );

void checkNoExtraArguments(const vector< string >& arguments);
vector< string > convertLineToShellArguments(const string& line);

shared_ptr< Progress > getDownloadProgress(const Config&);

#endif

