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
#include <sstream>

#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/binaryversion.hpp>

#include <internal/nativeresolver/score.hpp>

namespace cupt {
namespace internal {

ScoreManager::ScoreManager(const Config& config, const shared_ptr< const Cache >& cache)
	: __cache(cache)
{
	for (size_t i = 0; i < ScoreChange::SubScore::Count; ++i)
	{
		const char* leafOption;
		switch (ScoreChange::SubScore::Type(i))
		{
			case ScoreChange::SubScore::New:
				leafOption = "new"; break;
			case ScoreChange::SubScore::Removal:
				leafOption = "removal"; break;
			case ScoreChange::SubScore::Upgrade:
				leafOption = "upgrade"; break;
			case ScoreChange::SubScore::Downgrade:
				leafOption = "downgrade"; break;
			case ScoreChange::SubScore::QualityAdjustment:
				leafOption = "quality-adjustment"; break;
			case ScoreChange::SubScore::PositionPenalty:
				leafOption = "position-penalty"; break;
			case ScoreChange::SubScore::FailedRecommends:
				leafOption = "failed-recommends"; break;
			case ScoreChange::SubScore::FailedSuggests:
				leafOption = "failed-suggests"; break;
			case ScoreChange::SubScore::FailedSync:
				leafOption = "failed-synchronization"; break;
			default:
				fatal("internal error: missing score multiplier for the score '%zu'", i);
		}

		__subscore_multipliers[i] = config.getInteger(
				string("cupt::resolver::tune-score::") + leafOption);
	}
	__quality_bar = config.getInteger("cupt::resolver::quality-bar");
}

ssize_t ScoreManager::__get_version_weight(const shared_ptr< const BinaryVersion >& version) const
{
	return version ? __cache->getPin(version) : 0;
}

ScoreChange ScoreManager::getScoreChange(const shared_ptr< const BinaryVersion >& originalVersion,
		const shared_ptr< const BinaryVersion >& supposedVersion) const
{
	auto supposedVersionWeight = __get_version_weight(supposedVersion);
	auto originalVersionWeight = __get_version_weight(originalVersion);

	auto value = supposedVersionWeight - originalVersionWeight;

	ScoreChange::SubScore::Type scoreType;
	if (!originalVersion)
	{
		scoreType = ScoreChange::SubScore::New;
	}
	else if (!supposedVersion)
	{
		scoreType = ScoreChange::SubScore::Removal;
		if (value < 0 && originalVersion->essential)
		{
			value *= 5;
		}
	}
	else
	{
		scoreType = compareVersionStrings(originalVersion->versionString,
				supposedVersion->versionString) < 0 ? ScoreChange::SubScore::Upgrade : ScoreChange::SubScore::Downgrade;
	}

	ScoreChange scoreChange;
	scoreChange.__subscores[scoreType] = value;
	// quality correction makes backtracking more/less possible
	scoreChange.__subscores[ScoreChange::SubScore::QualityAdjustment] -= __quality_bar;

	return scoreChange;
}

ssize_t ScoreManager::getScoreChangeValue(const ScoreChange& scoreChange) const
{
	ssize_t result = 0;
	for (size_t i = 0; i < ScoreChange::SubScore::Count; ++i)
	{
		auto subValue = (ssize_t)(scoreChange.__subscores[i] * __subscore_multipliers[i]);
		if (i <= ScoreChange::SubScore::Downgrade)
		{
			subValue /= 10;
		}
		result += subValue;
	}
	return result;
}

string ScoreManager::getScoreChangeString(const ScoreChange& scoreChange) const
{
	return sf("%s=%zd", scoreChange.__to_string().c_str(), getScoreChangeValue(scoreChange));
}


ScoreChange::ScoreChange()
{
	for (size_t i = 0; i < SubScore::Count; ++i)
	{
		__subscores[i] = 0u;
	}
}

string ScoreChange::__to_string() const
{
	std::ostringstream result;
	for (size_t i = 0; i < SubScore::Count; ++i)
	{
		const ssize_t& subscore = __subscores[i];
		if (subscore != 0u)
		{
			if (!result.str().empty())
			{
				result << '/';
			}
			result << subscore;
			switch (i)
			{
				case SubScore::New:
					result << 'a'; break;
				case SubScore::Removal:
					result << 'r'; break;
				case SubScore::Upgrade:
					result << 'u'; break;
				case SubScore::Downgrade:
					result << 'd'; break;
				case SubScore::QualityAdjustment:
					result << 'q'; break;
				case SubScore::PositionPenalty:
					result << "pp"; break;
				case SubScore::FailedRecommends:
					result << "fr"; break;
				case SubScore::FailedSuggests:
					result << "fs"; break;
				case SubScore::FailedSync:
					result << "fy"; break;
			}
		}
	}

	return result.str();
}

void ScoreChange::setPosition(size_t position)
{
	__subscores[SubScore::PositionPenalty] = -(ssize_t)position;
}

void ScoreChange::setFailedRecommends()
{
	__subscores[SubScore::FailedRecommends] = -1;
}

void ScoreChange::setFailedSuggests()
{
	__subscores[SubScore::FailedSuggests] = -1;
}

void ScoreChange::setFailedSync()
{
	__subscores[SubScore::FailedSync] = -1;
}

}
}

