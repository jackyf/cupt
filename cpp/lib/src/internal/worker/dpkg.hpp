/**************************************************************************
*   Copyright (C) 2013 by Eugene V. Lyubimkin                             *
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
#ifndef CUPT_INTERNAL_WORKER_DPKG_SEEN
#define CUPT_INTERNAL_WORKER_DPKG_SEEN

#include <internal/worker/packages.hpp>

namespace cupt {
namespace internal {

class Dpkg
{
 public:
	Dpkg(WorkerBase*);
	~Dpkg();
	void doActionGroup(const InnerActionGroup&, const Worker::ActionsPreview&);
 private:
	void p_makeSureThatSystemIsTriggerClean();
	void p_runPendingTriggers();
	string p_getActionCommand(const string&, InnerAction::Type, const string&, const InnerActionGroup&);

	WorkerBase* p_base;
	string p_fullCommand;
	bool p_shouldDeferTriggers;
	string p_archivesDirectoryPath;
	bool p_debugging;
};


}
}

#endif

