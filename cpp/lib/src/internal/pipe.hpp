/**************************************************************************
*   Copyright (C) 2010-2012 by Eugene V. Lyubimkin                        *
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
#ifndef CUPT_PIPE_SEEN
#define CUPT_PIPE_SEEN

#include <cupt/common.hpp>

namespace cupt {
namespace internal {

struct PipeData;

class CUPT_API Pipe
{
	internal::PipeData* __data;
	Pipe(const Pipe&);
 public:
	Pipe(const string& name);
	virtual ~Pipe();
	void useAsReader();
	void useAsWriter();
	int getReaderFd();
	int getWriterFd();
};

}
}

#endif

