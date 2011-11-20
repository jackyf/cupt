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
#include <unistd.h>

#include "colorizer.hpp"

const string noColor = "\e[0m";

Colorizer::Colorizer(const Config& config)
{
	string optionName("cupt::console::use-colors");
	auto stringEnabledValue = config.getString(optionName);
	if (stringEnabledValue != "auto")
	{
		__enabled = config.getBool(optionName);
	}
	else // guessing...
	{
		__enabled = false;
		if (isatty(STDOUT_FILENO))
		{
			const char* term = getenv("TERM");
			if (term)
			{
				if (strcmp(term, "xterm") == 0 || strcmp(term, "linux") == 0)
				{
					__enabled = true;
				}
			}
		}
	}
}

string Colorizer::makeBold(const string& input) const
{
	if (__enabled)
	{
		return string("\e[1m") + input + noColor;
	}
	else
	{
		return input;
	}
}

string Colorizer::colorize(const string& input, Color color, bool bold) const
{
	if (color == Default)
	{
		return bold ? makeBold(input) : input;
	}

	if (__enabled)
	{
		return format2("\e[%c;3%cm", bold ? '1' : '0', color) + input + noColor;
	}
	else
	{
		return input;
	}
}

bool Colorizer::enabled() const
{
	return __enabled;
}

