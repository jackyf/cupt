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
#ifndef CUPT_CONFIG_SEEN
#define CUPT_CONFIG_SEEN

#include <cupt/common.hpp>

namespace cupt {

namespace internal {

struct ConfigImpl;

}

class Config
{
	internal::ConfigImpl* __impl;
 public:
	Config();
	virtual ~Config();

	Config(const Config& other);
	Config& operator=(const Config& other);

	vector< string > getScalarOptionNames() const;
	vector< string > getListOptionNames() const;

	void setScalar(const string& optionName, const string& value);
	void setList(const string& optionName, const string& value);

	vector< string > getList(const string& optionName) const;
	string getString(const string& optionName) const;
	bool getBool(const string& optionName) const;
	ssize_t getNumber(const string& optionName) const;
};

} // namespace

#endif

