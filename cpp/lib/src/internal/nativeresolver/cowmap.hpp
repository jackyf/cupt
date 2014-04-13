/**************************************************************************
*   Copyright (C) 2014 by Eugene V. Lyubimkin                             *
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
#ifndef CUPT_COW_MAP_SEEN
#define CUPT_COW_MAP_SEEN

namespace cupt {
namespace internal {

template < typename KeyT, typename MapT >
class CowMap
{
	const MapT* p_initial;
	shared_ptr< const MapT > p_master;
	shared_ptr< MapT > p_added;

	CowMap(const CowMap&) = delete;
 public:
	CowMap();
	void setInitialMap(const MapT*);
	void operator=(const CowMap&);

	// only for maps without removals
	template < typename Data >
	vector<const Data*> getEntries() const;

	template < typename DataT >
	const DataT* get(KeyT) const;
	template < typename DataT >
	void add(KeyT, DataT&&);
	void remove(KeyT);
	template < typename CallbackT >
	void foreachModifiedEntry(const CallbackT&) const;

	void shrinkToFit();
};

}
}

#endif

