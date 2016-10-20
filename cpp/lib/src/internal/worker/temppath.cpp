/**************************************************************************
*   Copyright (C) 2016 by Eugene V. Lyubimkin                             *
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
#include <internal/worker/temppath.hpp>

#include <internal/filesystem.hpp>

namespace cupt {
namespace internal {

TempPath::TempPath(const string& path)
	: p_path(path)
{}

TempPath::~TempPath()
{
	if (fs::fileExists(p_path))
	{
		if (!fs::remove(p_path))
		{
			warn2(__("unable to remove the file '%s'"), p_path);
		}
	}
}

SharedTempPath::SharedTempPath(const string& path)
	: p_impl(std::make_shared<TempPath>(path))
{}

void SharedTempPath::abandon()
{
	p_impl.reset();
}

}
}


