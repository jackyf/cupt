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
#ifndef CUPT_INTERNAL_NATIVERESOLVER_SCORE_SEEN
#define CUPT_INTERNAL_NATIVERESOLVER_SCORE_SEEN

#include <cupt/common.hpp>
#include <cupt/fwd.hpp>

namespace cupt {
namespace internal {

using cache::BinaryVersion;

class ScoreChange
{
	friend class ScoreManager;

	struct SubScore
	{
		enum Type { Version, New, Removal, RemovalOfEssential, RemovalOfAuto, Upgrade, Downgrade,
				UnsatisfiedRecommends, UnsatisfiedSuggests, FailedSync, PositionPenalty, Count };
	};

	ssize_t __subscores[SubScore::Count];

	string __to_string() const;

 public:
	ScoreChange();
	void setPosition(size_t);
};

class ScoreManager
{
	shared_ptr< const Cache > __cache;
	ssize_t __subscore_multipliers[ScoreChange::SubScore::Count];
	ssize_t __quality_adjustment;
	ssize_t __preferred_version_default_pin;

	ssize_t __get_version_weight(const shared_ptr< const BinaryVersion >& version) const;
 public:
	ScoreManager(const Config&, const shared_ptr< const Cache >&);
	ssize_t getScoreChangeValue(const ScoreChange&) const;
	ScoreChange getVersionScoreChange(const shared_ptr< const BinaryVersion >&,
			const shared_ptr< const BinaryVersion >&) const;
	ScoreChange getUnsatisfiedRecommendsScoreChange() const;
	ScoreChange getUnsatisfiedSuggestsScoreChange() const;
	ScoreChange getUnsatisfiedSynchronizationScoreChange() const;
	string getScoreChangeString(const ScoreChange&) const;
};

}
}

#endif

