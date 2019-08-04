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
#ifndef CUPT_COMMON_REGEX_SEEN
#define CUPT_COMMON_REGEX_SEEN

#include <boost/xpressive/xpressive_dynamic.hpp>

namespace cupt {

using boost::xpressive::sregex;
using boost::xpressive::sregex_token_iterator;
using boost::xpressive::smatch;
using boost::xpressive::ssub_match;

using boost::xpressive::cregex;
using boost::xpressive::cmatch;

using boost::xpressive::regex_match;
using boost::xpressive::regex_search;
using boost::xpressive::regex_error;

namespace regex_constants = boost::xpressive::regex_constants;

}

#endif

