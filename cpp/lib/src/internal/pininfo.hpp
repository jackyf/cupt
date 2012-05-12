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
#ifndef CUPT_INTERNAL_PININFO_SEEN
#define CUPT_INTERNAL_PININFO_SEEN

#include <boost/xpressive/xpressive_fwd.hpp>

#include <cupt/common.hpp>
#include <cupt/fwd.hpp>

namespace cupt {
namespace internal {

using cache::Version;

using boost::xpressive::sregex;

class PinInfo
{
	struct PinEntry
	{
		struct Condition
		{
			enum Type { SourcePackageName, PackageName, Version, ReleaseArchive, ReleaseCodename,
					ReleaseVendor, ReleaseVersion, ReleaseComponent, ReleaseLabel, HostName };

			Type type;
			shared_ptr< sregex > value;
		};

		vector< Condition > conditions;
		ssize_t priority;
	};

	shared_ptr< const Config > config;
	shared_ptr< const system::State > systemState;
	vector< PinEntry > settings;

	void init();
	void loadData(const string& path);
	ssize_t getOriginalAptPin(const Version*) const;
	void adjustUsingPinSettings(const Version*, ssize_t& priority) const;
 public:
	PinInfo(const shared_ptr< const Config >&, const shared_ptr< const system::State >&);

	ssize_t getPin(const Version*, const string& installedVersionString) const;
};

}
}

#endif

