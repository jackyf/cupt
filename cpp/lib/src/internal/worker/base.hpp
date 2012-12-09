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
#ifndef CUPT_INTERNAL_WORKER_BASE_SEEN
#define CUPT_INTERNAL_WORKER_BASE_SEEN

#include <sys/types.h>
#include <sys/stat.h>

#include <cupt/fwd.hpp>
#include <cupt/system/worker.hpp>

#include <internal/logger.hpp>

namespace cupt {
namespace internal {

using system::Worker;
using system::Resolver;
using system::State;

using namespace cache;

class Lock;

class WorkerBase
{
	mode_t __umask;
	Lock* __lock;
 protected:
	shared_ptr< const Config > _config;
	shared_ptr< const Cache > _cache;
	Logger* _logger;

	typedef Worker::ActionsPreview ActionsPreview;
	typedef Worker::Action Action;
	shared_ptr< const Resolver::SuggestedPackages > __desired_state;
	shared_ptr< ActionsPreview > __actions_preview;

	string _get_archives_directory() const;
	static string _get_archive_basename(const BinaryVersion*);
	void _run_external_command(Logger::Subsystem, const string&,
			const string& = "", const string& = "");

	static Action::Type _download_dependent_action_types[4];
 public:
	static const string partialDirectorySuffix;

	WorkerBase();
	WorkerBase(const shared_ptr< const Config >&, const shared_ptr< const Cache >&);
	virtual ~WorkerBase();
};

}
}

#endif

