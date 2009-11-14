#***************************************************************************
#*   Copyright (C) 2009 by Eugene V. Lyubimkin                             *
#*                                                                         *
#*   This program is free software; you can redistribute it and/or modify  *
#*   it under the terms of the GNU General Public License                  *
#*   (version 3 or above) as published by the Free Software Foundation.    *
#*                                                                         *
#*   This program is distributed in the hope that it will be useful,       *
#*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
#*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
#*   GNU General Public License for more details.                          *
#*                                                                         *
#*   You should have received a copy of the GNU GPL                        *
#*   along with this program; if not, write to the                         *
#*   Free Software Foundation, Inc.,                                       *
#*   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA               *
#*                                                                         *
#*   This program is free software; you can redistribute it and/or modify  *
#*   it under the terms of the Artistic License, which comes with Perl     *
#***************************************************************************
package Cupt::System::Resolvers::Native::PackageEntry;

use strict;
use warnings;

use Cupt::LValueFields qw(version stick fake_satisfied reasons checked_bits
		manually_selected installed);

sub new {
	my $class = shift;
	my $self = (bless [] => $class);
	$self->version = undef;
	$self->stick = 0;
	$self->fake_satisfied = [];
	$self->reasons = [];
	$self->checked_bits = 0;
	# next are not cloned intentionally
	$self->manually_selected = 0;
	$self->installed = 0;
	return $self;
}

sub clone {
	my ($self) = @_;

	my $cloned = bless [] => 'Cupt::System::Resolvers::Native::PackageEntry';
	$cloned->[version_offset()] = $self->[version_offset()];
	$cloned->[stick_offset()] = $self->[stick_offset()];
	$cloned->[fake_satisfied_offset()] = [ @{$self->[fake_satisfied_offset()]} ];
	$cloned->[reasons_offset()] = [ @{$self->[reasons_offset()]} ];
	$cloned->[checked_bits_offset()] = $self->[checked_bits_offset()];

	return $cloned;
}

1;

