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

#define TAG(tagNameParam, code) \
		{ \
			static const char tagNameParamString[] = #tagNameParam; \
			static const size_t tagNameParamStringSize = sizeof(tagNameParamString) - 1; \
			if ((size_t)(tagName.second - tagName.first) == tagNameParamStringSize && \
				!memcmp(&*tagName.first, tagNameParamString, tagNameParamStringSize)) \
			{ \
				code \
				continue; \
			} \
		}

// TODO: do string(tagValue) only one time
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
				string tagNameString(tagName); \
				if (tagNameString != "Package" && tagNameString != "Status") \
				{ \
					if (!v->others) \
					{ \
						v->others = new map< string, string >; \
					} \
					(*(v->others))[tagNameString] = tagValue; \
				} \
			}
#endif

