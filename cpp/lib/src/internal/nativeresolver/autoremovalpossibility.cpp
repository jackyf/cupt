/**************************************************************************
*   Copyright (C) 2011 by Eugene V. Lyubimkin                             *
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
#include <common/regex.hpp>

#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/binaryversion.hpp>

#include <internal/nativeresolver/autoremovalpossibility.hpp>

namespace cupt {
namespace internal {

class AutoRemovalPossibilityImpl
{
	mutable smatch __m;
	vector< sregex > __never_regexes;
	bool __can_autoremove;

	bool __never_autoremove(const string& packageName) const
	{
		FORIT(regexIt, __never_regexes)
		{
			if (regex_match(packageName, __m, *regexIt))
			{
				return true;
			}
		}
		return false;
	}
 public:
	AutoRemovalPossibilityImpl(const Config& config)
	{
		__can_autoremove = config.getBool("cupt::resolver::auto-remove");

		auto neverAutoRemoveRegexStrings = config.getList("apt::neverautoremove");
		FORIT(regexStringIt, neverAutoRemoveRegexStrings)
		{
			try
			{
				__never_regexes.push_back(sregex::compile(*regexStringIt));
			}
			catch (regex_error&)
			{
				fatal2("invalid regular expression '%s'", *regexStringIt);
			}
		}
	}

	bool isAllowed(const Cache& cache, const shared_ptr< const BinaryVersion >& version,
			bool wasInstalledBefore) const
	{
		const string& packageName = version->packageName;

		if (version->essential)
		{
			return false;
		}
		auto canAutoremoveThisPackage = __can_autoremove && cache.isAutomaticallyInstalled(packageName);
		if (wasInstalledBefore && !canAutoremoveThisPackage)
		{
			return false;
		}
		if (__never_autoremove(packageName))
		{
			return false;
		}

		return true;
	}
};

AutoRemovalPossibility::AutoRemovalPossibility(const Config& config)
{
	__impl = new AutoRemovalPossibilityImpl(config);
}

AutoRemovalPossibility::~AutoRemovalPossibility()
{
	delete __impl;
}

bool AutoRemovalPossibility::isAllowed(const Cache& cache, const shared_ptr< const BinaryVersion >& version,
		bool wasInstalledBefore) const
{
	return __impl->isAllowed(cache, version, wasInstalledBefore);
}

}
}

