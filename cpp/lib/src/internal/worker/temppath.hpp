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
#pragma once

#include <cupt/common.hpp>

#include <memory>

namespace cupt {
namespace internal {

class TempPath
{
 public:
	explicit TempPath(const string&);
	~TempPath();
	operator string() const { return p_path; }

	TempPath(TempPath&&) = delete;
 private:
	string p_path;
};

class SharedTempPath
{
 public:
	explicit SharedTempPath(const string& path);
	operator string() const { return static_cast<string>(*p_impl); }
	void abandon();
 private:
	shared_ptr<TempPath> p_impl;
};

}
}

