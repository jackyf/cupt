/**************************************************************************
*   Copyright (C) 2013 by Eugene V. Lyubimkin                             *
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
#include <thread>

namespace cupt {
namespace internal {

/* the whole point of this class is provide simple <future> facilities on
   systems which lack them (#727621) */

template< typename ResultT >
class ExceptionlessFuture
{
 public:
	template< typename FunctorT >
	ExceptionlessFuture(const FunctorT& functor)
	{
		p_thread = std::thread([this, functor]() { p_result = functor(); });
	}
	~ExceptionlessFuture()
	{
		if (p_thread.joinable())
		{
			p_thread.join();
		}
	}
	ResultT get()
	{
		p_thread.join();
		return p_result;
	}
 private:
	std::thread p_thread;
	ResultT p_result; // for our purposes and simplicity we assume copyability
};

}
}

