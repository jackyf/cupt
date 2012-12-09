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

/// @file

#include <cupt/common.hpp>
#include <cupt/fwd.hpp>

namespace cupt {

namespace internal {

class CacheImpl;
class StateData;

}

/** @namespace cupt::system */
namespace system {

/// stores an additional information for installed packages
class CUPT_API State
{
	internal::StateData* __data;
	State(const State&);
 public:
	/// installed package's information
	struct InstalledRecord
	{
		/// wanted package state
		struct Want
		{
			/// type
			enum Type { Unknown, Install, Hold, Deinstall, Purge, Count };
		};
		/// package state flag
		struct Flag
		{
			/// type
			enum Type { Ok, Reinstreq, Hold, HoldAndReinstreq, Count };
		};
		/// package installation status
		struct Status
		{
			/// type
			enum Type { NotInstalled, Unpacked, HalfConfigured, HalfInstalled, ConfigFiles,
				PostInstFailed, RemovalFailed, Installed, TriggersPending, TriggersAwaited, Count };
			static const string strings[]; ///< string values of correspoding types
		};
		Want::Type want;
		Flag::Type flag;
		Status::Type status;

		/// returns true when the package is not installed properly
		bool isBroken() const;
	};

	/// constructor, not for public use
	CUPT_LOCAL State(shared_ptr< const Config >, internal::CacheImpl*);
	/// destructor
	~State();

	/// gets installed record for a package
	/**
	 * @param packageName
	 * @return pointer to InstalledRecord if found, empty pointer if not
	 */
	shared_ptr< const InstalledRecord > getInstalledInfo(const string& packageName) const;
	/// gets installed package names
	/**
	 * @return array of package names
	 */
	vector< string > getInstalledPackageNames() const;
	/// gets system binary architecture
	string getArchitecture() const;
	/// @cond
	CUPT_LOCAL vector< string > getReinstallRequiredPackageNames() const;
	/// @endcond
};

}
}

#endif

