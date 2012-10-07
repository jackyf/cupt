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

#include <cupt/cache/package.hpp>
#include <cupt/cache/version.hpp>
#include <cupt/cache/binaryversion.hpp>

namespace cupt {
namespace internal {

template< typename VersionType >
BasePackageIterator< VersionType >::BasePackageIterator(UnderlyingIterator ui)
	: __ui(ui)
{}

template< typename VersionType >
auto BasePackageIterator< VersionType >::operator++() -> Self&
{
	++__ui;
	return *this;
}

template< typename VersionType >
auto BasePackageIterator< VersionType >::operator*() const -> const VersionType*
{
	return static_cast< const VersionType* >(__ui->get());
}

template< typename VersionType >
auto BasePackageIterator< VersionType >::operator==(const Self& other) const -> bool
{
	return __ui == other.__ui;
}

template< typename VersionType >
auto BasePackageIterator< VersionType >::operator!=(const Self& other) const -> bool
{
	return !(*this == other);
}

template class BasePackageIterator< cache::Version >;
template class BasePackageIterator< cache::BinaryVersion >;

}
}
