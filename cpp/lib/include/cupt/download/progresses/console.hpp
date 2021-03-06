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
#ifndef CUPT_DOWNLOAD_PROGRESSES_CONSOLE_SEEN
#define CUPT_DOWNLOAD_PROGRESSES_CONSOLE_SEEN

/// @file

#include <cupt/download/progress.hpp>

namespace cupt {

namespace internal {

class ConsoleProgressImpl;

}

namespace download {

/// console-based download progress meter
class CUPT_API ConsoleProgress: public Progress
{
	internal::ConsoleProgressImpl* __impl;
 protected:
	virtual void newDownloadHook(const string& uri, const DownloadRecord&);
	virtual void finishedDownloadHook(const string& uri, const string& result);
	virtual void updateHook(bool immediate);
	virtual void finishHook();
 public:
	/// constructor
	ConsoleProgress();
	/// destructor
	~ConsoleProgress();
};

}
}

#endif

