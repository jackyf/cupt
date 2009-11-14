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

use List::MoreUtils qw(uniq);

use Cupt::Core;
use Cupt::Cache;
use Cupt::Graph;

use Cupt::LValueFields qw(score level identifier finished pending_action
		_packages _master_packages _master_solution
		_cache _dependency_graph _dependency_groups);

sub __clone_packages ($) {
	my ($ref_packages) = @_;

	my %clone;
	foreach (keys %$ref_packages) {
		$clone{$_} = $ref_packages->{$_}->clone();
	}
	return \%clone;
}

sub new {
	my ($class, $cache, $ref_dependency_groups, $ref_packages) = @_;
	my $self = bless [] => $class;

	$self->score = 0;
	$self->level = 0;
	$self->identifier = 0;
	$self->finished = 0;
	$self->pending_action = undef;

	$self->_master_solution = undef;
	$self->_packages = __clone_packages($ref_packages);
	$self->_master_packages = undef;

	$self->_cache = $cache;
	$self->_dependency_graph = Cupt::Graph->new();
	$self->_dependency_groups = $ref_dependency_groups;

	$self->_initialize_dependency_graph();

	return $self;
}

sub _initialize_dependency_graph {
	my ($self) = @_;

	foreach my $package_name ($self->get_package_names()) {
		$self->_add_package_dependencies($package_name);
	}
}

sub clone {
	my ($self) = @_;

	my $cloned = Cupt::System::Resolvers::Native::Solution->new({});
	$cloned->score = $self->score;
	$cloned->level = $self->level;
	$cloned->identifier = undef; # will be set later :(
	$cloned->finished = 0;
	$cloned->pending_action = $self->pending_action;

	$cloned->_master_solution = $self;

	$cloned->_cache = $self->_cache;
	$cloned->_dependency_groups = $self->_dependency_groups;
	$cloned->_dependency_graph = $self->_dependency_graph;

	# other part should be done by calling prepare outside

	return $cloned;
}

sub prepare {
	my ($self) = @_;

	my $master_solution = $self->_master_solution;
	defined $master_solution or myinternaldie("undefined master solution");

	if (not defined $master_solution->_master_packages) {
		# this solution is a master solution, build a slave on top of it
		$self->_master_packages = $master_solution->_packages;
		$self->_packages = {};
	} else {
		# this a slave solution
		if (scalar keys %{$master_solution->_packages} >= (scalar keys %{$master_solution->_master_packages})**0.8) {
			# overdiverted solution, build a new master one
			$self->_master_packages = undef;
			$self->_packages = __clone_packages($master_solution->_master_packages);
			foreach my $key (keys %{$master_solution->_packages}) {
				$self->_packages->{$key} = $master_solution->_packages->{$key}->clone();
			}
		} else {
			# build new slave solution from current
			$self->_master_packages = $master_solution->_master_packages;
			$self->_packages = __clone_packages($master_solution->_packages);
		}
	}

	$self->_master_solution = undef;
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
		$self->_add_package_dependencies($package_name);
		$package_entry = Cupt::System::Resolvers::Native::PackageEntry->new();
	}

	$self->_invalidate($package_name, $package_entry);
	$self->_packages->{$package_name} = $package_entry;
}

sub add_version_dependencies {
	my ($self, $version) = @_;

	my $cache = $self->_cache;

	my $package_name = $version->package_name;

	foreach my $ref_dependency_group (@{$self->_dependency_groups}) {
		my $dependency_group_name = $ref_dependency_group->{'name'};
		my $ref_dependencies = $version->$dependency_group_name;
		my @satisfying_versions = map { @{$cache->get_satisfying_versions($_)} } @$ref_dependencies;
		my @satisfying_package_names = uniq map { $_->package_name } @satisfying_versions;

		foreach my $satisfying_package_name (@satisfying_package_names) {
			next if $satisfying_package_name eq $package_name;
			$self->_dependency_graph->add_edge($satisfying_package_name, $package_name);
			if ($ref_dependency_group->{'target'} eq 'anti') {
				$self->_dependency_graph->add_edge($package_name, $satisfying_package_name);
			}
		}
	}
}

sub _add_package_dependencies {
	my ($self, $package_name) = @_;

	return if $package_name eq $Cupt::System::Resolvers::Native::dummy_package_name;

	my $package = $self->_cache->get_binary_package($package_name);
	defined $package or
			myinternaldie("unable to find the package '$package_name'");
	my @versions = @{$package->get_versions()};

	foreach my $version (@versions) {
		$self->add_version_dependencies($version);
	}
}

sub _invalidate {
	my ($self, $package_name, $package_entry) = @_;

	$package_entry->checked_bits = '';
	foreach my $successor_package_name ($self->_dependency_graph->successors($package_name)) {
		my $successor_package_entry = $self->get_package_entry($successor_package_name);
		defined $successor_package_entry or next;
		if (not exists $self->_packages->{$successor_package_name}) {
			# this is package entry from _master_packages, and we change it, so we
			# need to clone it
			$self->_packages->{$successor_package_name} =
					$successor_package_entry = $successor_package_entry->clone();
		}
		$successor_package_entry->checked_bits = '';
	}
}

sub validate {
	my ($self, $package_name, $dependency_group_index) = @_;

	my $package_entry = $self->get_package_entry($package_name);

	if (not exists $self->_packages->{$package_name}) {
		# this is package entry from _master_packages, and we change it, so we
		# need to clone it
		$self->_packages->{$package_name} =
				$package_entry = $package_entry->clone();
	}
	vec($package_entry->checked_bits, $dependency_group_index, 1) = 1;
}

1;

