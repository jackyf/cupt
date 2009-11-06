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
use Cupt::LValueFields qw(score level identifier
		finished _packages _master_packages _divert_level);

my $_max_divert_level = 16;

sub __clone_packages ($) {
	my ($ref_packages) = @_;

	my %clone;
	foreach (keys %$ref_packages) {
		$clone{$_} = $ref_packages->{$_}->clone();
	}
	return \%clone;
}

sub new {
	my ($class, $ref_packages) = @_;
	my $self = bless [] => $class;
	$self->_packages = __clone_packages($ref_packages);
	$self->score = 0;
	$self->level = 0;
	$self->identifier = 0;
	$self->finished = 0;
	$self->_master_packages = undef;
	$self->_divert_level = 0;
	return $self;
}

sub clone {
	my ($self) = @_;

	my $cloned = Cupt::System::Resolvers::Native::Solution->new({});
	$cloned->score = $self->score;
	$cloned->level = $self->level;
	$cloned->identifier = undef; # will be set later :(
	$cloned->finished = 0;

	if ($self->_divert_level == $_max_divert_level) {
		$cloned->_divert_level = 0;
		$cloned->_master_packages = undef;
		$cloned->_packages = __clone_packages($self->_master_packages);
		foreach my $key (keys %{$self->_packages}) {
			$cloned->_packages->{$key} = $self->_packages->{$key}->clone();
		}
	} else {
		$cloned->_divert_level = $self->_divert_level + 1;
		if (defined $self->_master_packages) {
			$cloned->_master_packages = $self->_master_packages;
		   	$cloned->_packages = __clone_packages($self->_packages);
		} else {
			$cloned->_master_packages = $self->_packages;
			$cloned->_packages = {};
		}
	}
	return $cloned;
}

sub get_package_names {
	my ($self) = @_;

	if (not defined $self->_master_packages) {
		return keys %{$self->_packages};
	} else {
		return ((grep { not exists $self->_master_packages->{$_} } keys %{$self->_packages}),
				keys %{$self->_master_packages});
	}
}

sub get_package_entry {
	my ($self, $package_name) = @_;

	return $self->[_packages_offset()]->{$package_name} //
			$self->[_master_packages_offset()]->{$package_name};
}

sub set_package_entry {
	my ($self, $package_name) = @_;
	my $package_entry = $self->get_package_entry($package_name);
	if (defined $package_entry) {
		$package_entry = $package_entry->clone();
	} else {
		$package_entry = Cupt::System::Resolvers::Native::PackageEntry->new();
	}
	$self->_packages->{$package_name} = $package_entry;
}

1;

