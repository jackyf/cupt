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

/// @file

#include <cupt/common.hpp>

namespace cupt {

namespace internal {

class ConfigImpl;

}

/// stores library's configuration variables
class CUPT_API Config
{
	internal::ConfigImpl* __impl;
 public:
	/// constructor
	/**
	 * Reads configuration variables from configuration files.
	 */
	Config();
	/// destructor
	virtual ~Config();

	/// copy constructor
	Config(const Config& other);
	/// assignment operator
	Config& operator=(const Config& other);

	/// returns scalar option names
	vector<string> getScalarOptionNames() const;
	/// returns list option names
	vector<string> getListOptionNames() const;

	/// sets new value for the scalar option
	/**
	 * @param optionName
	 * @param value new value for the option
	 */
	void setScalar(const string& optionName, const string& value);
	/// appends new element to the value of the list option
	/**
	 * @param optionName
	 * @param value new value element for the option
	 */
	void setList(const string& optionName, const string& value);

	/// gets contents of the list variable
	/**
	 * @param optionName
	 */
	vector< string > getList(const string& optionName) const;
	/// gets value of the scalar option
	/**
	 * @param optionName
	 */
	string getString(const string& optionName) const;
	/// gets converted to boolean value of the scalar option
	/**
	 * @param optionName
	 */
	bool getBool(const string& optionName) const;
	/// gets converted to integer value of the scalar option
	/**
	 * @param optionName
	 */
	ssize_t getInteger(const string& optionName) const;
	/// gets resolved value of the path variable
	/**
	 * @param optionName
	 */
	string getPath(const string& optionName) const;
	/// gets paths of non-ignored configuration part files
	/**
	 * @param optionName
	 */
	vector<string> getConfigurationPartPaths(const string& optionName) const;
};

} // namespace

#endif

