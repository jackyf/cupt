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
#include <cupt/cache/binarypackage.hpp>

#include <internal/nativeresolver/score.hpp>

namespace cupt {
namespace internal {

ScoreManager::ScoreManager(const Config& config, const shared_ptr< const Cache >& cache)
	: __cache(cache)
{
	__quality_adjustment = config.getInteger("cupt::resolver::score::quality-adjustment");
	__preferred_version_default_pin = config.getString("apt::default-release").empty() ?
			500 : 990;
	__subscore_multipliers[ScoreChange::SubScore::Version] = 1u;
	// from 1, skipping SubScore::Version
	for (size_t i = 1; i < ScoreChange::SubScore::Count; ++i)
	{
		const char* leafOption;
		switch (ScoreChange::SubScore::Type(i))
		{
			case ScoreChange::SubScore::New:
				leafOption = "new"; break;
			case ScoreChange::SubScore::Removal:
				leafOption = "removal"; break;
			case ScoreChange::SubScore::RemovalOfEssential:
				leafOption = "removal-of-essential"; break;
			case ScoreChange::SubScore::RemovalOfAuto:
				leafOption = "removal-of-autoinstalled"; break;
			case ScoreChange::SubScore::Upgrade:
				leafOption = "upgrade"; break;
			case ScoreChange::SubScore::Downgrade:
				leafOption = "downgrade"; break;
			case ScoreChange::SubScore::PositionPenalty:
				leafOption = "position-penalty"; break;
			case ScoreChange::SubScore::UnsatisfiedRecommends:
				leafOption = "unsatisfied-recommends"; break;
			case ScoreChange::SubScore::UnsatisfiedSuggests:
				leafOption = "unsatisfied-suggests"; break;
			case ScoreChange::SubScore::FailedSync:
				leafOption = "failed-synchronization"; break;
			default:
				fatal2i("missing score multiplier for the score '%zu'", i);
		}

		__subscore_multipliers[i] = config.getInteger(
				string("cupt::resolver::score::") + leafOption);
	}
}

ssize_t ScoreManager::__get_version_weight(const BinaryVersion* version) const
{
	return version ? (__cache->getPin(version) - __preferred_version_default_pin) : 0;
}

ScoreChange ScoreManager::getVersionScoreChange(const BinaryVersion* originalVersion,
		const BinaryVersion* supposedVersion) const
{
	auto supposedVersionWeight = __get_version_weight(supposedVersion);
	auto originalVersionWeight = __get_version_weight(originalVersion);

	auto value = supposedVersionWeight - originalVersionWeight;

	ScoreChange scoreChange;

	ScoreChange::SubScore::Type scoreType;
	if (!originalVersion)
	{
		scoreType = ScoreChange::SubScore::New;
	}
	else if (!supposedVersion)
	{
		scoreType = ScoreChange::SubScore::Removal;

		auto binaryPackage = __cache->getBinaryPackage(originalVersion->packageName);
		auto installedVersion = binaryPackage->getInstalledVersion();
		if (installedVersion && installedVersion->essential)
		{
			scoreChange.__subscores[ScoreChange::SubScore::RemovalOfEssential] = 1;
		}
		if (__cache->isAutomaticallyInstalled(originalVersion->packageName))
		{
			scoreChange.__subscores[ScoreChange::SubScore::RemovalOfAuto] = 1;
		}
	}
	else
	{
		scoreType = compareVersionStrings(originalVersion->versionString,
				supposedVersion->versionString) < 0 ? ScoreChange::SubScore::Upgrade : ScoreChange::SubScore::Downgrade;
	}

	scoreChange.__subscores[ScoreChange::SubScore::Version] = value;
	scoreChange.__subscores[scoreType] = 1;

	return scoreChange;
}

ScoreChange ScoreManager::getUnsatisfiedRecommendsScoreChange() const
{
	ScoreChange result;
	result.__subscores[ScoreChange::SubScore::UnsatisfiedRecommends] = 1;
	return result;
}

ScoreChange ScoreManager::getUnsatisfiedSuggestsScoreChange() const
{
	ScoreChange result;
	result.__subscores[ScoreChange::SubScore::UnsatisfiedSuggests] = 1;
	return result;
}

ScoreChange ScoreManager::getUnsatisfiedSynchronizationScoreChange() const
{
	ScoreChange result;
	result.__subscores[ScoreChange::SubScore::FailedSync] = 1;
	return result;
}

ssize_t ScoreManager::getScoreChangeValue(const ScoreChange& scoreChange) const
{
	// quality correction makes backtracking more/less possible
	ssize_t result = __quality_adjustment;

	for (size_t i = 0; i < ScoreChange::SubScore::Count; ++i)
	{
		auto subValue = (ssize_t)(scoreChange.__subscores[i] * __subscore_multipliers[i]);
		result += subValue;
	}
	return result;
}

string ScoreManager::getScoreChangeString(const ScoreChange& scoreChange) const
{
	return format2("%s=%zd", scoreChange.__to_string(), getScoreChangeValue(scoreChange));
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
			if (subscore != 1u)
			{
				result << subscore;
			}
			switch (i)
			{
				case SubScore::Version:
					result << 'v'; break;
				case SubScore::New:
					result << 'a'; break;
				case SubScore::Removal:
					result << 'r'; break;
				case SubScore::RemovalOfEssential:
					result << "re"; break;
				case SubScore::RemovalOfAuto:
					result << "ra"; break;
				case SubScore::Upgrade:
					result << 'u'; break;
				case SubScore::Downgrade:
					result << 'd'; break;
				case SubScore::PositionPenalty:
					result << "pp"; break;
				case SubScore::UnsatisfiedRecommends:
					result << "ur"; break;
				case SubScore::UnsatisfiedSuggests:
					result << "us"; break;
				case SubScore::FailedSync:
					result << "fs"; break;
			}
		}
	}

	return result.str();
}

void ScoreChange::setPosition(size_t position)
{
	__subscores[SubScore::PositionPenalty] = position;
}

}
}

