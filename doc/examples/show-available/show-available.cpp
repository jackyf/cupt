/**************************************************************************
*   Copyright (C) 2015 by Eugene V. Lyubimkin                             *
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

/// prints all package versions available in the metadata cache

#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/binarypackage.hpp>

#include <algorithm>
#include <iostream>

using std::cout;
using std::make_shared;

int main()
{
	cupt::Version::parseInfoOnly = false;
	cupt::Version::parseRelations = false;

	auto config = make_shared<cupt::Config>();
	auto cache = make_shared<cupt::Cache>(config, false, true, false);

	auto packageNames = cache->getBinaryPackageNames().asVector();
	std::sort(packageNames.begin(), packageNames.end());

	for (const auto& packageName: packageNames)
	{
		cout << packageName << ": [";

		auto package = cache->getBinaryPackage(packageName);

		size_t index = 0;
		for (auto elem: cache->getSortedVersionsWithPriorities(package))
		{
			if (index++ > 0)
			{
				cout << ", ";
			}
			cout << elem.version->versionString;
		}

		cout << "]\n";
	}
}

