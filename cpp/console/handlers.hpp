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

#ifndef HANDLERS_SEEN
#define HANDLERS_SEEN

#include "common.hpp"
#include "misc.hpp"

int search(Context&);
int showBinaryVersions(Context&);
int showSourceVersions(Context&);
int showRelations(Context&, bool);
int dumpConfig(Context&);
int policy(Context&, bool);
int shell(Context&);
int showPackageNames(Context&);
int findDependencyChain(Context&);
int updateReleaseAndIndexData(Context&);
int downloadSourcePackage(Context&);
int changeAutoInstalledState(Context&, bool);
int cleanArchives(Context&, bool);
int showScreenshotUris(Context&);
int snapshot(Context&);
int tarMetadata(Context&);
int showAutoInstalled(Context&);

struct ManagePackages
{
	enum Mode { FullUpgrade, SafeUpgrade, Install, Reinstall, Purge, Remove,
			Satisfy, Unsatisfy, Markauto, Unmarkauto, BuildDepends, LoadSnapshot,
			InstallIfInstalled };
};
int managePackages(Context&, ManagePackages::Mode);
int distUpgrade(Context&);

struct ChangelogOrCopyright
{
	enum Type { Changelog, Copyright };
};

int downloadChangelogOrCopyright(Context& context, ChangelogOrCopyright::Type);

extern bool shellMode;

#endif

