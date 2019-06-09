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
#include <cstdlib>

#include "colorizer.hpp"

namespace {

const string noColor = "\e[0m";

bool guessColorSupport() {
	if (isatty(STDOUT_FILENO))
	{
		const char* term = getenv("TERM");
		if (term)
		{
			if (strcmp(term, "xterm") == 0 ||
				strncmp(term, "xterm-", 6) == 0 ||
				strcmp(term, "linux") == 0)
			{
				return true;
			}
		}
	}

	return false;
}

}

Colorizer::Colorizer(const Config& config)
{
	const string optionName("cupt::console::use-colors");
	const auto strValue = config.getString(optionName);
	__enabled = (strValue == "auto" ? guessColorSupport() : config.getBool(optionName));
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
		auto boldPrefix = bold ? "1;" : "";
		return format2("\e[%s3%cm", boldPrefix, color) + input + noColor;
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

