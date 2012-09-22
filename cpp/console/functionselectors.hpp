/**************************************************************************
*   Copyright (C) 2012 by Eugene V. Lyubimkin                             *
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
#ifndef CUPT_CONSOLE_FUNCTION_SELECTORS_SEEN
#define CUPT_CONSOLE_FUNCTION_SELECTORS_SEEN

#include <memory>
#include <list>

#include "common.hpp"

using std::unique_ptr;
using std::list;

class FunctionSelector
{
 protected:
	FunctionSelector();
 public:
	virtual ~FunctionSelector();
};

unique_ptr< FunctionSelector > parseFunctionQuery(const string&, bool);

list< const Version* > selectAllVersions(const Cache&, const FunctionSelector&);
list< const Version* > selectBestVersions(const Cache&, const FunctionSelector&);

#endif

