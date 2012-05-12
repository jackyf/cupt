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
	vector< sregex > __no_if_rdepends_regexes;
	bool __can_autoremove;

	void __fill_regexes(const Config& config, const string& optionName, vector< sregex >& regexes)
	{
		for (const auto& regexString: config.getList(optionName))
		{
			try
			{
				regexes.push_back(sregex::compile(regexString));
			}
			catch (regex_error&)
			{
				fatal2(__("invalid regular expression '%s'"), regexString);
			}
		}
	}

	bool __matches_regexes(const string& packageName, const vector< sregex >& regexes) const
	{
		FORIT(regexIt, regexes)
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
		__fill_regexes(config, "apt::neverautoremove", __never_regexes);
		__fill_regexes(config, "cupt::resolver::no-autoremove-if-rdepends-exist", __no_if_rdepends_regexes);
	}

	typedef AutoRemovalPossibility::Allow Allow;
	Allow isAllowed(const Cache& cache, const BinaryVersion* version,
			bool wasInstalledBefore) const
	{
		const string& packageName = version->packageName;

		if (version->essential)
		{
			return Allow::No;
		}
		auto canAutoremoveThisPackage = __can_autoremove && cache.isAutomaticallyInstalled(packageName);
		if (wasInstalledBefore && !canAutoremoveThisPackage)
		{
			return Allow::No;
		}
		if (__matches_regexes(packageName, __never_regexes))
		{
			return Allow::No;
		}
		if (__matches_regexes(packageName, __no_if_rdepends_regexes))
		{
			return Allow::YesIfNoRDepends;
		}

		return Allow::Yes;
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

AutoRemovalPossibility::Allow AutoRemovalPossibility::isAllowed(const Cache& cache, const BinaryVersion* version,
		bool wasInstalledBefore) const
{
	return __impl->isAllowed(cache, version, wasInstalledBefore);
}

}
}

