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
#ifndef CUPT_INTERNAL_CONFIGPARSER_SEEN
#define CUPT_INTERNAL_CONFIGPARSER_SEEN

#include <functional>

#include <boost/xpressive/xpressive_fwd.hpp>

#include <cupt/common.hpp>

namespace cupt {
namespace internal {

using boost::xpressive::sregex;

class ConfigParser
{
 public:
	typedef std::function< void (const string&, const string&) > Handler;
 private:
	typedef string::const_iterator sci;

	Handler __regular_handler;
	Handler __list_handler;
	Handler __clear_handler;

	sci __end;
	sci __current;
	vector< string > __errors;
	string __read;
	string __option_prefix;

	void __statements();
	bool __statement();
	bool __simple_or_nested_or_list();
	bool __clear();
	bool __value();
	bool __name();
	bool __semicolon();
	bool __opening_bracket();
	bool __closing_bracket();
	bool __regex(const sregex&);
	bool __string(const char*);
	void __skip_spaces();
	void __maybe_error(const string&);
	void __error_out();
 public:
	ConfigParser(Handler regularHandler, Handler listHandler, Handler clearHandler);
	void parse(const string& path);
};

} // namespace
} // namespace

#endif

