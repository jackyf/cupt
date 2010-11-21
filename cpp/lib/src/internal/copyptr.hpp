/**************************************************************************
*   Copyright (C) 2005-2007, 2010 by Eugene V. Lyubimkin                  *
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
#ifndef CUPT_INTERNAL_COPYPTR_SEEN
#define CUPT_INTERNAL_COPYPTR_SEEN

namespace cupt {
namespace internal {

/// sole ownership of a single object, copyable
template < typename T >
class CopyPtr
{
 private:
	T* __ptr;
 public:
	CopyPtr() :
		__ptr(NULL)
	{}

	explicit CopyPtr(T* obj)
		: __ptr(obj)
	{}

	void initIfEmpty()
	{
		if (!__ptr)
		{
			__ptr = new T;
		}
	}

	CopyPtr(const CopyPtr& other) :
		__ptr(other.__ptr ? new T(*(other.__ptr)) : NULL)
	{};

	CopyPtr& operator=(const CopyPtr& other)
	{
		if (this != &other)
		{
			delete __ptr;
			if (other.__ptr)
			{
				__ptr = new T(*(other.__ptr));
			}
			else
			{
				__ptr = NULL;
			}
		}
		return *this;
	}

	CopyPtr& operator=(T* ptr)
	{
		if (__ptr != ptr)
		{
			delete __ptr;
			__ptr = ptr;
		}
		return *this;
	}

	~CopyPtr()
	{
		delete __ptr;
	}

	T* operator ->() const
	{
		return __ptr;
	}
	T& operator *() const
	{
		return *__ptr;
	}

	operator bool() const
	{
		return __ptr != NULL;
	}
};

}
}

#endif

