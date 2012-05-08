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
#ifndef CUPT_INTERNAL_COMMON_SEEN
#define CUPT_INTERNAL_COMMON_SEEN

#include <sys/wait.h>

#include <cupt/common.hpp>

namespace cupt {
namespace internal {

void chomp(string& str);

vector< string > split(char, const string&, bool allowEmpty = false);

string getWaitStatusDescription(int status);

// we may use following instead of boost::lexical_cast<> because of speed
uint32_t string2uint32(pair< string::const_iterator, string::const_iterator > input);

bool architectureMatch(const string& architecture, const string& pattern);

void processSpaceCommaSpaceDelimitedStrings(const char* begin, const char* end,
		const std::function< void (const char*, const char*) >& callback);
void processSpaceCommaSpaceDelimitedStrings(string::const_iterator begin, string::const_iterator end,
		const std::function< void (string::const_iterator, string::const_iterator) >& callback);
void processSpacePipeSpaceDelimitedStrings(string::const_iterator begin, string::const_iterator end,
		const std::function< void (string::const_iterator, string::const_iterator) >& callback);

} // namespace
} // namespace

#define N__(arg) arg

#endif

