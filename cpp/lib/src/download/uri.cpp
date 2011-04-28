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

#include <cupt/download/uri.hpp>

namespace cupt {
namespace internal {

struct UriData
{
	string uri;
	size_t colonPosition;
	size_t hostStartPosition;
};

}

namespace download {

Uri::Uri(const string& uri)
{
	__data = new internal::UriData;
	__data->uri = uri;
	__data->colonPosition = uri.find(':');
	if (__data->colonPosition == string::npos || __data->colonPosition == uri.size() - 1)
	{
		fatal("unable to find scheme(protocol) in URI '%s'", uri.c_str());
	}

	// a valid position since colonPosition is verified to be not last
	__data->hostStartPosition = __data->colonPosition + 1;

	if (uri[__data->hostStartPosition] == '/')
	{
		// "//" is dropped
		if (uri.size() < __data->hostStartPosition + 2 || uri[__data->hostStartPosition+1] != '/')
		{
			fatal("there should be no or two slashes after a colon in URI '%s'", uri.c_str());
		}
		__data->hostStartPosition += 2;
	}
}

Uri::~Uri()
{
	delete __data;
}

Uri::Uri(const Uri& other)
{
	__data = new internal::UriData(*(other.__data));
}

Uri& Uri::operator=(const Uri& other)
{
	if (this == &other)
	{
		return *this;
	}
	delete __data;
	__data = new internal::UriData(*(other.__data));
	return *this;
}

Uri::operator string() const
{
	return __data->uri;
}

string Uri::getProtocol() const
{
	return __data->uri.substr(0, __data->colonPosition);
}

string Uri::getHost() const
{
	auto hostEndPosition = __data->uri.find("/", __data->hostStartPosition);
	if (hostEndPosition == string::npos)
	{
		hostEndPosition = __data->uri.size();
	}
	auto result = __data->uri.substr(__data->hostStartPosition, hostEndPosition - __data->hostStartPosition);
	auto credentialsEndPosition = result.rfind('@');
	if (credentialsEndPosition != string::npos)
	{
		result.erase(0, credentialsEndPosition+1);
	}
	auto portPreStartPosition = result.find(':');
	if (portPreStartPosition != string::npos)
	{
		result.erase(portPreStartPosition);
	}
	return result;
}

string Uri::getOpaque() const
{
	return __data->uri.substr(__data->hostStartPosition);
}

}
}
