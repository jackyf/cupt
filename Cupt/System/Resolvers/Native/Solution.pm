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
package Cupt::System::Resolvers::Native::Solution;

use strict;
use warnings;

# packages => PackageEntry
# score' => score
# level' => level
# identifier' => identifier
# finished' => finished (1 | 0)
use Cupt::LValueFields qw(packages score level identifier finished);

sub __clone_packages ($) {
	my ($ref_packages) = @_;

	my %clone;
	foreach (keys %$ref_packages) {
		my $package_entry = $ref_packages->{$_};

		my $new_package_entry = ($clone{$_} = []);
		bless $new_package_entry => 'Cupt::System::Resolvers::Native::PackageEntry';

		# $new_package_entry->version = $package_entry->version;
		# $new_package_entry->stick = $package_entry->stick;
		# $new_package_entry->fake_satisfied = [ @{$package_entry->fake_satisfied} ];
		# $new_package_entry->reasons = [ @{$package_entry->reasons} ];
		#
		# see the above code? it's obviously right
		# but it's so slow...
		#
		# keep the following in sync with package entry structure
		$new_package_entry->[0] = $package_entry->[0];
		$new_package_entry->[1] = $package_entry->[1];
		$new_package_entry->[2] = [ @{$package_entry->[2]} ];
		$new_package_entry->[3] = [ @{$package_entry->[3]} ];
	}
	return \%clone;
}

sub new {
	my ($class, $ref_packages) = @_;
	my $self = bless [] => $class;
	$self->packages = __clone_packages($ref_packages);
	$self->score = 0;
	$self->level = 0;
	$self->identifier = 0;
	$self->finished = 0;
	return $self;
}

sub clone {
	my ($self) = @_;

	my $cloned = Cupt::System::Resolvers::Native::Solution->new($self->packages);
	$cloned->score = $self->score;
	$cloned->level = $self->level;
	$cloned->identifier = undef; # will be set later :(
	$cloned->finished = 0;

	return $cloned;
}

1;

