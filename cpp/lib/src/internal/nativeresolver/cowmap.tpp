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
#ifndef CUPT_COW_MAP_IMPL_SEEN
#define CUPT_COW_MAP_IMPL_SEEN


namespace cupt {
namespace internal {

namespace {

template < typename MapT, typename CallbackT >
void cowMapForeach(const MapT& masterEntries, const MapT& addedEntries, CallbackT callback)
{
	typedef typename MapT::value_type DataT;
	class CallbackIterator: public std::iterator< std::output_iterator_tag, DataT >
	{
		const CallbackT& __callback;
	 public:
		CallbackIterator(const CallbackT& callback_)
			: __callback(callback_) {}
		CallbackIterator& operator++() { return *this; }
		CallbackIterator& operator*() { return *this; }
		void operator=(const typename MapT::value_type& data)
		{
			__callback(data);
		}
	};
	struct Comparator
	{
		bool operator()(const DataT& left, const DataT& right)
		{ return left.first < right.first; }
	};
	// it's important that parent's __added_entries come first,
	// if two elements are present in both (i.e. an element is overriden)
	// the new version of an element will be considered
	std::set_union(addedEntries.begin(), addedEntries.end(),
			masterEntries.begin(), masterEntries.end(),
			CallbackIterator(callback), Comparator());
}

}


template < typename KeyT, typename MapT >
CowMap<KeyT,MapT>::CowMap()
{
	p_added.reset(new MapT);
}

template < typename KeyT, typename MapT >
void CowMap<KeyT,MapT>::setInitialMap(const MapT* map)
{
	p_initial = map;
}

template < typename KeyT, typename MapT >
template < typename DataT >
bool CowMap<KeyT,MapT>::add(KeyT newKey, DataT&& data)
{
	auto it = p_added->lower_bound(newKey);
	if (it == p_added->end() || it->first != newKey)
	{
		// there is no modifiable element in this solution
		p_added->insert(it, { newKey, std::move(data) });
		return true;
	}
	else
	{
		bool dataWasPresent = it->second;
		it->second = std::move(data);
		return !dataWasPresent;
	}
}

template < typename KeyT, typename MapT >
void CowMap<KeyT,MapT>::remove(KeyT key)
{
	auto it = p_added->lower_bound(key);
	if (it != p_added->end() && it->first == key)
	{
		typedef typename MapT::value_type::second_type DataT;
		it->second = DataT();
	}
	else
	{
		p_added->insert(it, { key, {} });
	}
}

template < typename KeyT, typename MapT >
void CowMap<KeyT,MapT>::operator=(const CowMap& parent)
{
	p_initial = parent.p_initial;

	if (!parent.p_master)
	{
		// parent is a master mix, build a slave on top of it
		p_master = parent.p_added;
	}
	else
	{
		// this a slave mix
		size_t& forkedCount = parent.p_master->forkedCount;
		forkedCount += parent.p_added->size();
		if (forkedCount > parent.p_master->size())
		{
			forkedCount = 0;

			// parent mix is overdiverted, build new master one
			p_added->reserve(parent.p_added->size() + parent.p_master->size());

			cowMapForeach(*parent.p_master, *parent.p_added,
					[this](const typename MapT::value_type& data) { this->p_added->push_back(data); });
		}
		else
		{
			// build new slave mix from current
			p_master = parent.p_master;
			*p_added = *(parent.p_added);
		}
	}
}

template < typename KeyT, typename MapT >
vector<KeyT> CowMap<KeyT,MapT>::getKeys() const
{
	vector<KeyT> result;

	static const MapT nullMap;
	const auto& initial = *p_initial;
	const auto& master = p_master ? *p_master : nullMap;

	MapT intermediateMap;
	cowMapForeach(initial, master,
			[&intermediateMap](const typename MapT::value_type& data) { intermediateMap.push_back(data); });
	cowMapForeach(intermediateMap, *p_added,
			[&result](const typename MapT::value_type& data) { if (data.second) result.push_back(data.first); });

	return result;
}

template < typename KeyT, typename MapT >
template < typename CallbackT >
void CowMap<KeyT,MapT>::foreachModifiedEntry(const CallbackT& callback) const
{
	static const MapT nullMap;
	const auto& master = p_master ? *p_master : nullMap;

	cowMapForeach(master, *p_added, callback);
}

template < typename KeyT, typename MapT >
template < typename DataT >
const DataT* CowMap<KeyT,MapT>::get(KeyT key) const
{
	auto it = p_added->find(key);
	if (it != p_added->end())
	{
		return &it->second;
	}
	if (p_master)
	{
		it = p_master->find(key);
		if (it != p_master->end())
		{
			return &it->second;
		}
	}
	it = p_initial->find(key);
	if (it != p_initial->end())
	{
		return &it->second;
	}

	return nullptr; // not found
}

}
}

#endif

