/**************************************************************************
*   Copyright (C) 2011 by Eugene V. Lyubimkin                             *
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
#ifndef CUPT_INTERNAL_NATIVERESOLVER_AUTOREMOVALPOSSIBILITY
#define CUPT_INTERNAL_NATIVERESOLVER_AUTOREMOVALPOSSIBILITY

#include <cupt/fwd.hpp>

namespace cupt {
namespace internal {

using cupt::cache::BinaryVersion;

class AutoRemovalPossibilityImpl;

class AutoRemovalPossibility
{
	AutoRemovalPossibilityImpl* __impl;
 public:
	AutoRemovalPossibility(const Config&);
	~AutoRemovalPossibility();

	enum class Allow { Yes, No, YesIfNoRDepends };
	Allow isAllowed(const BinaryVersion*, bool, bool) const;
};

}
}

#endif

