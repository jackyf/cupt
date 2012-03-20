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
#include <boost/lexical_cast.hpp>

#include <cupt/config.hpp>
#include <cupt/download/method.hpp>
#include <cupt/download/uri.hpp>

namespace cupt {
namespace download {

Method::Method()
{}

string Method::getAcquireSuboptionForUri(const shared_ptr< const Config >& config,
		const Uri& uri, const string& suboptionName)
{
	auto host = uri.getHost();
	// this "zoo" of per-host variants is given by APT...
	string optionNamePrefix = string("acquire::") + uri.getProtocol();
	auto result = config->getString(optionNamePrefix + "::" + suboptionName + "::" + host);
	if (result.empty())
	{
		result = config->getString(optionNamePrefix + "::" + host + "::" + suboptionName);
	}
	if (result.empty())
	{
		result = config->getString(optionNamePrefix + "::" + suboptionName);
	}
	return result;
}

ssize_t Method::getIntegerAcquireSuboptionForUri(const shared_ptr< const Config >& config,
		const Uri& uri, const string& suboptionName)
{
	auto result = getAcquireSuboptionForUri(config, uri, suboptionName);
	ssize_t numericResult = 0;
	if (!result.empty())
	{
		try
		{
			numericResult = boost::lexical_cast< ssize_t >(result); // checking is it numeric
		}
		catch (std::exception&)
		{
			fatal2(__("the value '%s' of the suboption '%s' is not numeric"), result, suboptionName);
		}
	}
	return numericResult;
}

}
}

