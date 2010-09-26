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
#ifndef CUPT_SYSTEM_STATE_SEEN
#define CUPT_SYSTEM_STATE_SEEN

#include <cupt/common.hpp>
#include <cupt/fwd.hpp>

namespace cupt {

namespace internal {

class CacheImpl;
class StateData;

}

namespace system {

class State
{
	internal::StateData* __data;
	State(const State&);
 public:
	struct InstalledRecord
	{
		struct Want
		{
			enum Type { Unknown, Install, Hold, Deinstall, Purge, Count };
		};
		struct Flag
		{
			enum Type { Ok, Reinstreq, Hold, HoldAndReinstreq, Count };
		};
		struct Status
		{
			enum Type { NotInstalled, Unpacked, HalfConfigured, HalfInstalled, ConfigFiles,
				PostInstFailed, RemovalFailed, Installed, TriggersPending, TriggersAwaited, Count };
			static const string strings[];
		};
		Want::Type want;
		Flag::Type flag;
		Status::Type status;
	};

	State(shared_ptr< const Config >, internal::CacheImpl*);
	~State();

	shared_ptr< const InstalledRecord > getInstalledInfo(const string& packageName) const;
	vector< string > getInstalledPackageNames() const;
};

}
}

#endif

