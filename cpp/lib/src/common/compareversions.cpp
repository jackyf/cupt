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

#include <cstring>

#include <cupt/common.hpp>
#include <cupt/versionstring.hpp>

#include <internal/common.hpp>

namespace cupt {

typedef pair< string::const_iterator, string::const_iterator > StringAnchorPair;

void __divide_versions_parts(const string& versionString, StringAnchorPair& epoch,
		StringAnchorPair& upstream, StringAnchorPair& revision)
{
	epoch.first = versionString.begin();

	auto position = versionString.rfind(versionstring::idSuffixDelimiter);
	if (position != string::npos)
	{
		revision.second = versionString.begin() + position;
	}
	else
	{
		revision.second = versionString.end();
	}

	position = versionString.find(':');
	if (position != string::npos)
	{
		// we found an epoch
		auto it = versionString.begin() + position;
		epoch.second = it;
		upstream.first = it + 1;
	}
	else
	{
		epoch.second = epoch.first;
		upstream.first = versionString.begin();
	}

	position = versionString.rfind('-');
	if (position != string::npos)
	{
		// found a revision
		auto it = versionString.begin() + position;
		upstream.second = it;
		revision.first = it + 1;
	}
	else
	{
		upstream.second = revision.second;
		revision.first = revision.second;
	}
}

void __consume_number(string::const_iterator& substringStart,
		string::const_iterator& substringEnd, const string::const_iterator& end)
{
	// skipping leading zeroes
	while (substringStart != end && *substringStart == '0')
	{
		++substringStart;
		++substringEnd;
	}
	while (substringEnd != end && isdigit(*substringEnd))
	{
		++substringEnd;
	}
}

static short inline __get_modified_ascii_value(char c)
{
	// all alphas sort before others
	return isalpha(c) ? short(c) - 1000 : short(c);
}

int __compare_version_part(StringAnchorPair left, StringAnchorPair right)
{
	string::const_iterator leftSubstringStart;
	string::const_iterator rightSubstringStart;
	string::const_iterator leftSubstringEnd = left.first;
	string::const_iterator rightSubstringEnd = right.first;
	const string::const_iterator& leftEnd = left.second;
	const string::const_iterator& rightEnd = right.second;

	bool numberMode = false;

	do
	{
		leftSubstringStart = leftSubstringEnd;
		rightSubstringStart = rightSubstringEnd;
		if (numberMode)
		{
			__consume_number(leftSubstringStart, leftSubstringEnd, leftEnd);
			__consume_number(rightSubstringStart, rightSubstringEnd, rightEnd);
			auto leftSubstringLength = leftSubstringEnd - leftSubstringStart;
			auto rightSubstringLength = rightSubstringEnd - rightSubstringStart;
			if (leftSubstringLength < rightSubstringLength)
			{
				return -1;
			}
			else if (leftSubstringLength > rightSubstringLength)
			{
				return 1;
			}
			auto compareResult = memcmp(&*leftSubstringStart, &*rightSubstringStart, leftSubstringLength);
			if (compareResult != 0)
			{
				return compareResult;
			}
		}
		else
		{
			// string mode
			while (leftSubstringEnd != leftEnd && rightSubstringEnd != rightEnd &&
					(!isdigit(*leftSubstringEnd) || !isdigit(*rightSubstringEnd)))
			{
				if (*leftSubstringEnd != *rightSubstringEnd)
				{
					if (*leftSubstringEnd == '~')
					{
						return -1;
					}
					else if (*rightSubstringEnd == '~')
					{
						return 1;
					}
					else if (isdigit(*leftSubstringEnd))
					{
						return -1;
					}
					else if (isdigit(*rightSubstringEnd))
					{
						return 1;
					}
					else
					{
						auto leftValue = __get_modified_ascii_value(*leftSubstringEnd);
						auto rightValue = __get_modified_ascii_value(*rightSubstringEnd);
						return (leftValue < rightValue) ? -1 : 1;
					}
				}
				else
				{
					++leftSubstringEnd;
					++rightSubstringEnd;
				}
			}
			if (leftSubstringEnd != leftEnd && rightSubstringEnd == rightEnd)
			{
				return (*leftSubstringEnd == '~') ? -1 : 1;
			}
			if (leftSubstringEnd == leftEnd && rightSubstringEnd != rightEnd)
			{
				return (*rightSubstringEnd == '~') ? 1 : -1;
			}
		}
		numberMode = !numberMode;
	} while (leftSubstringEnd != leftEnd || rightSubstringEnd != rightEnd);

	return 0;
}

int compareVersionStrings(const string& left, const string& right)
{
	StringAnchorPair leftEpochMatch, leftUpstreamMatch, leftRevisionMatch;
	StringAnchorPair rightEpochMatch, rightUpstreamMatch, rightRevisionMatch;

	__divide_versions_parts(left, leftEpochMatch, leftUpstreamMatch, leftRevisionMatch);
	__divide_versions_parts(right, rightEpochMatch, rightUpstreamMatch, rightRevisionMatch);

	uint32_t leftEpoch;
	if (leftEpochMatch.first == leftEpochMatch.second)
	{
		leftEpoch = 0;
	}
	else
	{
		leftEpoch = internal::string2uint32(leftEpochMatch);
	}

	uint32_t rightEpoch;
	if (rightEpochMatch.first == rightEpochMatch.second)
	{
		rightEpoch = 0;
	}
	else
	{
		rightEpoch = internal::string2uint32(rightEpochMatch);
	}

	if (leftEpoch < rightEpoch)
	{
		return -1;
	}
	if (leftEpoch > rightEpoch)
	{
		return 1;
	}

	auto upstreamComparisonResult = __compare_version_part(leftUpstreamMatch, rightUpstreamMatch);
	if (upstreamComparisonResult != 0)
	{
		return upstreamComparisonResult;
	}

	static const string zeroRevision = "0";
	static const auto zeroRevisionAnchorPair = make_pair(zeroRevision.begin(), zeroRevision.end());

	// ok, checking revisions
	const StringAnchorPair* leftAnchorPair;
	if (leftRevisionMatch.first == leftRevisionMatch.second)
	{
		leftAnchorPair = &zeroRevisionAnchorPair;
	}
	else
	{
		leftAnchorPair = &leftRevisionMatch;
	}

	const StringAnchorPair* rightAnchorPair;
	if (rightRevisionMatch.first == rightRevisionMatch.second)
	{
		rightAnchorPair = &zeroRevisionAnchorPair;
	}
	else
	{
		rightAnchorPair = &rightRevisionMatch;
	}

	return __compare_version_part(*leftAnchorPair, *rightAnchorPair);
}

}

