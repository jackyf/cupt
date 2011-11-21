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
using std::cerr;

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
		auto systemState = cache.getSystemState();
		auto packageNames = systemState->getInstalledPackageNames();

		for (size_t i = 0; i < packageNames.size(); ++i)
		{
			const cupt::string& packageName = packageNames[i];
			if (cache.isAutomaticallyInstalled(packageName))
			{
				continue;
			}
			auto package = cache.getBinaryPackage(packageName);
			if (!package)
			{
				cupt::fatal("no binary package '%s'", packageName.c_str());
			}
			auto installedVersion = package->getInstalledVersion();
			if (!installedVersion)
			{
				cupt::fatal("no installed version in package '%s'", packageName.c_str());
			}
			auto policyVersion = cache.getPolicyVersion(package);
			if (!policyVersion)
			{
				cupt::fatal("unable to get policy version for package '%s'", packageName.c_str());
			}

			if (installedVersion->versionString != policyVersion->versionString)
			{
				cout << packageName << ": " << installedVersion->versionString
						<< " -> " << policyVersion->versionString << endl;
			}
		}
	}
	catch (cupt::Exception&)
	{
		return EXIT_FAILURE;
	}
	return 0;
}

