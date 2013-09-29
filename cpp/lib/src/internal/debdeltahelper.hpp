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
#ifndef CUPT_INTERNAL_DEBDELTAHELPER_SEEN
#define CUPT_INTERNAL_DEBDELTAHELPER_SEEN

#include <map>

#include <cupt/fwd.hpp>
#include <cupt/common.hpp>

namespace cupt {
namespace internal {

class DebdeltaHelper
{
	std::map< string, std::map< string, string > > __sources;
	void __parse_sources(const string&);
 public:
	struct DownloadRecord
	{
		string baseUri;
		string uri;
	};
	DebdeltaHelper();
	vector< DownloadRecord > getDownloadInfo(const cache::BinaryVersion*,
			const shared_ptr< const Cache >&);
};

}
}

#endif

