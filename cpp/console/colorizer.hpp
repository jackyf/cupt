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
#ifndef COLORIZER_SEEN
#define COLORIZER_SEEN

#include "common.hpp"

class Colorizer
{
	bool __enabled;
 public:
	enum Color { Default = ' ', Red = '1', Green = '2', Blue = '4', Yellow = '3', Cyan = '6', Magenta = '5' };
	Colorizer(const Config& config);
	string makeBold(const string& input) const;
	string colorize(const string& input, Color, bool bold) const;
	bool enabled() const;
};

#endif

