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

#include <internal/horspoolsearcher.hpp>

#ifndef CUPT_TAGPARSE_MACRO_SEEN
#define CUPT_TAGPARSE_MACRO_SEEN

#define TAG_CUSTOM(tagName, subRegex, code) \
		{ \
			static sregex currentRegex = sregex::compile("^" #tagName ": " subRegex, \
					regex_constants::optimize | regex_constants::not_dot_newline); \
			if (regex_search(block, m, currentRegex)) \
			{ \
				code \
				if (parseOthers) \
				{ \
					block.replace(m[0].first - block.begin(), m[0].second - m[0].first, ""); \
				} \
			} \
		}

namespace cupt {

class TagValue: public pair< string::const_iterator, string::const_iterator >
{
 public:
	operator string() const
	{
		return string(first, second);
	}
};

}

#define TAG(tagName, code) \
		{ \
			static const char tagString[] = #tagName ": "; \
			static const size_t tagStringSize = sizeof(tagString) - 1; \
			size_t position; \
			bool found = false; \
			if (block.compare(0, tagStringSize, tagString) == 0) \
			{ \
				found = true; \
				position = 0; \
			} \
			else \
			{ \
				static const internal::HorspoolSearcher fastSearcher("\n" #tagName ": "); \
				position = fastSearcher.searchIn(block); \
				if (position != string::npos) \
				{ \
					found = true; \
					++position; \
				} \
			} \
			if (found) \
			{ \
				tagValue.first = block.begin() + position + tagStringSize; \
				tagValue.second = block.begin() + block.find('\n', position + tagStringSize); \
				code \
				if (parseOthers) \
				{ \
					block.erase(position, tagValue.second - block.begin() - position); \
				} \
			} \
		}

#define TAG_MULTILINE(tagName, code) \
		{ \
			TagValue remainder; \
			TAG(tagName, \
			{ \
				remainder.second = remainder.first = tagValue.second + 1; \
				{ \
					auto end = block.end(); \
					while (remainder.second != end) \
					{ \
						if (!isblank(*remainder.second)) \
						{ \
							break; \
						} \
						++remainder.second; \
						while (*remainder.second != '\n') \
						{ \
							++remainder.second; \
						} \
						++remainder.second; \
					} \
				} \
				code \
				if (parseOthers) \
				{ \
					block.erase(remainder.first - block.begin(), \
							remainder.second - remainder.first); \
				} \
			}) \
		}

#define PARSE_PRIORITY \
		TAG(Priority, \
		{ \
			if (string(tagValue) == "required") \
			{ \
				v->priority = Version::Priorities::Required; \
			} \
			else if (string(tagValue) == "important") \
			{ \
				v->priority = Version::Priorities::Important; \
			} \
			else if (string(tagValue) == "standard") \
			{ \
				v->priority = Version::Priorities::Standard; \
			} \
			else if (string(tagValue) == "optional") \
			{ \
				v->priority = Version::Priorities::Optional; \
			} \
			else if (string(tagValue) == "extra") \
			{ \
				v->priority = Version::Priorities::Extra; \
			} \
			else \
			{ \
				fatal("bad priority value '%s'", string(tagValue).c_str()); \
			} \
		})

#define PARSE_OTHERS \
			if (Version::parseOthers) \
			{ \
				v->others = new map< string, string >; \
				static sregex fieldRegex = sregex::compile("^([A-Za-z-]+): (.*)$", \
						regex_constants::optimize | regex_constants::not_dot_newline); \
				string::const_iterator start, end; \
				smatch otherMatchResults; \
				start = block.begin(); \
				end = block.end(); \
				while (regex_search(start, end, otherMatchResults, fieldRegex)) \
				{ \
					start = otherMatchResults[0].second; \
					string match1(otherMatchResults[1]); \
					if (match1 == "Package" || match1 == "Status") \
					{ \
						continue; \
					} \
					(*(v->others))[match1] = string(otherMatchResults[2]); \
				} \
			}
#endif

