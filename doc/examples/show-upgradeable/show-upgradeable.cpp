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

/// shows installed and preferred versions for all upgradeable manually installed packages.

#include <iostream>
using std::cout;
using std::endl;

#include <cupt/common.hpp>
#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/system/state.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/binaryversion.hpp>

int main()
{
	try
	{
		cupt::shared_ptr< cupt::Config > config(new cupt::Config());
		cupt::Cache cache(config, false, true, true); // include binary installed packages

		for (const auto& packageName: cache.getSystemState()->getInstalledPackageNames())
		{
			if (cache.isAutomaticallyInstalled(packageName))
			{
				continue;
			}
			auto package = cache.getBinaryPackage(packageName);
			if (!package)
			{
				cupt::fatal2("no binary package '%s'", packageName);
			}
			auto installedVersion = package->getInstalledVersion();
			if (!installedVersion)
			{
				cupt::fatal2("no installed version in package '%s'", packageName);
			}
			auto preferredVersion = cache.getPreferredVersion(package);
			if (!preferredVersion)
			{
				cupt::fatal2("unable to get preferred version for package '%s'", packageName);
			}

			if (installedVersion != preferredVersion)
			{
				cout << packageName << ": " << installedVersion->versionString
						<< " -> " << preferredVersion->versionString << endl;
			}
		}
	}
	catch (cupt::Exception&)
	{
		return EXIT_FAILURE;
	}
	return 0;
}

