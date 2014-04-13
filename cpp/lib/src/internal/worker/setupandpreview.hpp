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
#ifndef CUPT_INTERNAL_WORKER_SETUPANDPREVIEW_SEEN
#define CUPT_INTERNAL_WORKER_SETUPANDPREVIEW_SEEN

#include <cupt/common.hpp>
#include <cupt/fwd.hpp>

#include <internal/worker/base.hpp>

namespace cupt {
namespace internal {

class SetupAndPreviewWorker: public virtual WorkerBase
{
	map< string, bool > __purge_overrides;

	void __generate_action_preview(const string&,
			const Resolver::SuggestedPackage&, bool);
	void __generate_actions_preview();
 public:
	void setDesiredState(const Resolver::Offer& offer);
	void setPackagePurgeFlag(const string&, bool);

	shared_ptr< const ActionsPreview > getActionsPreview() const;
	map< string, ssize_t > getUnpackedSizesPreview() const;
	pair< size_t, size_t > getDownloadSizesPreview() const;
};

}
}

#endif

