#include <boost/xpressive/xpressive_dynamic.hpp>

#define FORIT(variableName, storage) for (auto variableName = (storage).begin(), variableName##End = (storage).end(); variableName != variableName##End; ++variableName)

#include <cupt/common.hpp>

namespace cupt {

template < class T >
struct PointerLess
{
	bool operator()(const shared_ptr< T >& left, const shared_ptr< T >& right) const
	{
		return *left < *right;
	}
	bool operator()(const T* left, const T* right) const
	{
		return *left < *right;
	}
};
template < class T >
struct PointerEqual: public std::binary_function< shared_ptr< T >, shared_ptr< T >, bool >
{
	bool operator()(const shared_ptr< T >& left, const shared_ptr< T >& right) const
	{
		return *left == *right;
	}
	bool operator()(const T* left, const T* right) const
	{
		return *left == *right;
	}
};

}

