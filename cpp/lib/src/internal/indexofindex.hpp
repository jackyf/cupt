/**************************************************************************
*   Copyright (C) 2012 by Eugene V. Lyubimkin                             *
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

namespace cupt {
namespace internal {
namespace ioi {

struct Record
{
	uint32_t* offsetPtr;
	string* packageNamePtr;
};
// index suffix number must be incremented every time Record changes

struct Callbacks
{
	std::function< void () > main;
	std::function< void (const char*, const char*) > provides;
};

void processIndex(const string& path, const Callbacks&, const Record&);
string getIndexOfIndexPath(const string& path);
void removeIndexOfIndex(const string& path);
void generate(const string& indexPath, const string& temporaryPath);

}
}
}

