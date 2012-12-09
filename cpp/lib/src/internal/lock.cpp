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
#include <sys/file.h>

#include <cupt/config.hpp>
#include <cupt/file.hpp>

#include <internal/lock.hpp>

namespace cupt {
namespace internal {

Lock::Lock(const Config& config, const string& path)
	: __path(path), __file_ptr(NULL)
{
	__simulating = config.getBool("cupt::worker::simulate") ||
			!config.getBool("cupt::worker::use-locks");
	__debugging = config.getBool("debug::worker");

	if (__debugging)
	{
		debug2("obtaining lock '%s'", __path);
	}
	if (!__simulating)
	{
		string errorString;
		__file_ptr = new File(__path, "w", errorString);
		if (!errorString.empty())
		{
			fatal2(__("unable to open the file '%s': %s"), __path, errorString);
		}
		__file_ptr->lock(LOCK_EX | LOCK_NB);
	}
}

Lock::~Lock()
{
	if (__debugging)
	{
		debug2("releasing lock '%s'", __path);
	}
	if (!__simulating)
	{
		__file_ptr->lock(LOCK_UN);
		delete __file_ptr;
	}
}

}
}

