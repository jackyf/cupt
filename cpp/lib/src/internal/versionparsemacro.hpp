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
#ifndef CUPT_TAGPARSE_MACRO_SEEN
#define CUPT_TAGPARSE_MACRO_SEEN

#define TAG(tagNameParam, code) \
		{ \
			static const char tagNameParamString[] = #tagNameParam; \
			static const size_t tagNameParamStringSize = sizeof(tagNameParamString) - 1; \
			if (tagName.equal(tagNameParamString, tagNameParamStringSize)) \
			{ \
				code \
				continue; \
			} \
		}

// TODO: do string(tagValue) only one time
#define PARSE_PRIORITY \
		TAG(Priority, \
		{ \
			if (tagValue.equal("required", 8)) \
			{ \
				v->priority = Version::Priorities::Required; \
			} \
			else if (tagValue.equal("important", 9)) \
			{ \
				v->priority = Version::Priorities::Important; \
			} \
			else if (tagValue.equal("standard", 8)) \
			{ \
				v->priority = Version::Priorities::Standard; \
			} \
			else if (tagValue.equal("optional", 8)) \
			{ \
				v->priority = Version::Priorities::Optional; \
			} \
			else if (tagValue.equal("extra", 5)) \
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
				if (!tagName.equal("Package", 7) && !tagName.equal("Status", 6)) \
				{ \
					if (!v->others) \
					{ \
						v->others = new map< string, string >; \
					} \
					string tagNameString(tagName); \
					(*(v->others))[tagNameString] = tagValue; \
				} \
			}
#endif

