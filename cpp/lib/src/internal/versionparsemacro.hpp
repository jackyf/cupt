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
#ifndef CUPT_TAGPARSE_MACRO_SEEN
#define CUPT_TAGPARSE_MACRO_SEEN

#define TAG(tagNameParam, code) \
		{ \
			if (tagName.equal(BUFFER_AND_SIZE( #tagNameParam ))) \
			{ \
				code \
				continue; \
			} \
		}

#define PARSE_PRIORITY \
		TAG(Priority, \
		{ \
			if (tagValue.equal(BUFFER_AND_SIZE("required"))) \
			{ \
				v->priority = Version::Priorities::Required; \
			} \
			else if (tagValue.equal(BUFFER_AND_SIZE("important"))) \
			{ \
				v->priority = Version::Priorities::Important; \
			} \
			else if (tagValue.equal(BUFFER_AND_SIZE("standard"))) \
			{ \
				v->priority = Version::Priorities::Standard; \
			} \
			else if (tagValue.equal(BUFFER_AND_SIZE("optional"))) \
			{ \
				v->priority = Version::Priorities::Optional; \
			} \
			else if (tagValue.equal(BUFFER_AND_SIZE("extra"))) \
			{ \
				v->priority = Version::Priorities::Extra; \
			} \
			else \
			{ \
				warn2("package %s, version %s: unrecognized priority value '%s', using 'extra' instead", \
						v->packageName, v->versionString, string(tagValue)); \
			} \
		})

#define PARSE_OTHERS \
			if (Version::parseOthers) \
			{ \
				if (!tagName.equal(BUFFER_AND_SIZE("Package")) && !tagName.equal(BUFFER_AND_SIZE("Status"))) \
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

