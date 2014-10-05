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
#include <cupt/system/resolver.hpp>

#include <internal/nativeresolver/score.hpp>

namespace cupt {
namespace internal {

ScoreManager::ScoreManager(const Config& config, const shared_ptr< const Cache >& cache)
	: __cache(cache)
{
	__version_factor = config.getInteger("cupt::resolver::score::version-factor");
	__negative_version_factor = config.getInteger("cupt::resolver::score::negative-version-factor");
	__preferred_version_default_pin = config.getString("apt::default-release").empty() ?
			500 : 990;

	for (size_t i = 0; i < ScoreChange::SubScore::Count; ++i)
	{
		const auto type = ScoreChange::SubScore::Type(i);
		auto& multiplier = __subscore_multipliers[i];

		if (type == ScoreChange::SubScore::Version || type == ScoreChange::SubScore::UnsatisfiedCustomRequest)
		{
			multiplier = 1u;
			continue;
		}

		const char* leafOption;
		switch (type)
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
			case ScoreChange::SubScore::UnsatisfiedTry:
				leafOption = "unsatisfied-try"; break;
			case ScoreChange::SubScore::UnsatisfiedWish:
				leafOption = "unsatisfied-wish"; break;
			default:
				fatal2i("missing score multiplier for the score '%zu'", i);
		}

		multiplier = config.getInteger(
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

	auto value = p_getFactoredVersionScore(supposedVersionWeight - originalVersionWeight);

	ScoreChange scoreChange;
	scoreChange.__subscores[ScoreChange::SubScore::Version] = value;

	auto includeSubScore = [&scoreChange](ScoreChange::SubScore::Type sst)
	{
		scoreChange.__subscores[sst] = 1;
	};

	if (!originalVersion)
	{
		includeSubScore(ScoreChange::SubScore::New);
	}
	else if (!supposedVersion)
	{
		includeSubScore(ScoreChange::SubScore::Removal);

		if (originalVersion->essential)
		{
			includeSubScore(ScoreChange::SubScore::RemovalOfEssential);
		}
		if (__cache->isAutomaticallyInstalled(originalVersion->packageName))
		{
			includeSubScore(ScoreChange::SubScore::RemovalOfAuto);
		}
	}
	else
	{
		auto comparisonResult = compareVersionStrings(
				originalVersion->versionString, supposedVersion->versionString);
		if (comparisonResult < 0)
		{
			includeSubScore(ScoreChange::SubScore::Upgrade);
		}
		else if (comparisonResult > 0)
		{
			includeSubScore(ScoreChange::SubScore::Downgrade);
		}
	}

	return scoreChange;
}

ssize_t ScoreManager::p_getFactoredVersionScore(ssize_t inputScore) const
{
	auto s1 = inputScore * __version_factor / 100;
	auto s2 = s1 >= 0 ? s1 : (s1 * __negative_version_factor / 100);
	return s2;
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

ScoreChange ScoreManager::getCustomUnsatisfiedScoreChange(Resolver::RequestImportance importance) const
{
	ScoreChange result;
	if (importance == Resolver::RequestImportance::Try)
	{
		result.__subscores[ScoreChange::SubScore::UnsatisfiedTry] = 1;
	}
	else if (importance == Resolver::RequestImportance::Wish)
	{
		result.__subscores[ScoreChange::SubScore::UnsatisfiedWish] = 1;
	}
	else
	{
		result.__subscores[ScoreChange::SubScore::UnsatisfiedCustomRequest] = -(ssize_t)importance;
	}
	return result;
}

ssize_t ScoreManager::getScoreChangeValue(const ScoreChange& scoreChange) const
{
	// quality correction makes backtracking more/less possible
	ssize_t result = qualityAdjustment;

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
				case SubScore::UnsatisfiedTry:
					result << "ut"; break;
				case SubScore::UnsatisfiedWish:
					result << "uw"; break;
				case SubScore::UnsatisfiedCustomRequest:
					result << "uc"; break;
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

