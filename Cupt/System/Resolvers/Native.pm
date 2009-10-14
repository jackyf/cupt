#***************************************************************************
#*   Copyright (C) 2008-2009 by Eugene V. Lyubimkin                        *
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
package Cupt::System::Resolvers::Native;

=head1 NAME

Cupt::System::Resolvers::Native - native (built-in) dependency problem resolver for Cupt

=cut

use 5.10.0;
use strict;
use warnings;

use base qw(Cupt::System::Resolver);

use List::Util qw(reduce first);
use List::MoreUtils qw(any);

use Cupt::Core;
use Cupt::Cache::Relation qw(stringify_relation_expression);
use Cupt::System::Resolvers::Native::PackageEntry;
use Cupt::Graph;

our $_dummy_package_name = '<satisfy>';

=begin internal

=head2 _pending_relations

array of relations which are to be satisfied by final resolver, used for
filling depends, recommends (optionally), suggests (optionally) of requested
packages, or for satisfying some requested relations

=end internal

=cut

use fields qw(_old_packages _packages _pending_relations
		_strict_satisfy_relation_expressions _strict_unsatisfy_relation_expressions);

sub new {
	my $class = shift;
	my $self = fields::new($class);
	$self->SUPER::new(@_);

	$self->{_pending_relations} = [];
	$self->{_strict_satisfy_relation_expressions} = [];
	$self->{_strict_unsatisfy_relation_expressions} = [];

	return $self;
}


sub import_installed_versions ($$) {
	my ($self, $ref_versions) = @_;

	foreach my $version (@$ref_versions) {
		# just moving versions to packages, don't try install or remove some dependencies
		# '_packages' will be modified, leave '_old_packages' as original system state
		my $package_name = $version->package_name;
		$self->{_packages}->{$package_name} = Cupt::System::Resolvers::Native::PackageEntry->new();
		$self->{_packages}->{$package_name}->version = $version;
		$self->{_packages}->{$package_name}->installed = 1;
		@{$self->{_old_packages}->{$package_name}} = @{$self->{_packages}->{$package_name}};
		bless $self->{_old_packages}->{$package_name} => 'Cupt::System::Resolvers::Native::PackageEntry';
	}
	return;
}

sub _schedule_new_version_relations ($$) {
	my ($self, $version) = @_;

	# unconditionally adding pre-depends
	foreach (@{$version->pre_depends}) {
		$self->_auto_satisfy_relation($_, [ 'relation expression', $version, 'pre-depends', $_ ]);
	}
	# unconditionally adding depends
	foreach (@{$version->depends}) {
		$self->_auto_satisfy_relation($_, [ 'relation expression', $version, 'depends', $_ ]);
	}
	return;
}

sub __related_binary_package_names ($$) {
	my ($ref_packages, $version) = @_;

	my @result;

	my $package_name = $version->package_name;
	my $source_package_name = $version->source_package_name;

	foreach my $other_package_name (keys %$ref_packages) {
		my $other_version = $ref_packages->{$other_package_name}->version;
		next if not defined $other_version;
		next if $other_version->source_package_name ne $source_package_name;
		next if $other_package_name eq $package_name;
		push @result, $other_package_name;
	}

	return @result;
}

sub _get_package_version_by_source_version_string ($$) {
	my ($self, $package_name, $source_version_string) = @_;

	foreach my $version (@{$self->cache->get_binary_package($package_name)->get_versions()}) {
		if ($version->source_version_string eq $source_version_string) {
			return $version;
		}
	}

	return undef;
}

sub _related_packages_can_be_synchronized ($$) {
	my ($self, $ref_packages, $version) = @_;

	my $package_name = $version->package_name;
	my @related_package_names = __related_binary_package_names($ref_packages, $version);
	my $source_version_string = $version->source_version_string;

	foreach my $other_package_name (@related_package_names) {
		my $other_version = $ref_packages->{$other_package_name}->version;
		if (defined $other_version &&
			$other_version->source_version_string eq $source_version_string)
		{
			# no update needed
			return 1;
		}

		if ($ref_packages->{$other_package_name}->stick) {
			# cannot update the package
			return 0;
		}
		if (not defined $self->_get_package_version_by_source_version_string(
				$other_package_name, $source_version_string))
		{
			return 0;
		}
	}

	# ok, no errors
	return 1;
}

sub _synchronize_related_packages ($$$$$) {
	# $stick - boolean
	my ($self, $ref_packages, $version, $stick, $sub_mydebug_wrapper) = @_;

	my @related_package_names = __related_binary_package_names($ref_packages, $version);
	my $source_version_string = $version->source_version_string;
	my $package_name = $version->package_name;

	foreach my $other_package_name (@related_package_names) {
		my $package_entry = $ref_packages->{$other_package_name};
		next if $package_entry->stick;
		my $candidate_version = $self->_get_package_version_by_source_version_string(
				$other_package_name, $source_version_string);
		next if not defined $candidate_version;
		next if $candidate_version->version_string eq $package_entry->version->version_string;
		$package_entry->version = $candidate_version;
		$package_entry->stick = $stick;
		if ($self->config->var('debug::resolver')) {
			$sub_mydebug_wrapper->("synchronizing package '$other_package_name' with package '$package_name");
		}
		if ($self->config->var('cupt::resolver::track-reasons')) {
			push @{$package_entry->reasons}, [ 'sync', $package_name ];
		}
	}

	# ok, no errors
	return 1;
}

# installs new version, schedules new dependencies, but not sticks it
sub _install_version_no_stick ($$$) {
	my ($self, $version, $reason) = @_;

	my $package_name = $version->package_name;
	$self->{_packages}->{$package_name} //= Cupt::System::Resolvers::Native::PackageEntry->new();
	if ($self->{_packages}->{$package_name}->stick)
	{
		# package is restricted to be updated

		# but maybe nothing changed?
		my $existent_version = $self->{_packages}->{$package_name}->version;
		if (defined $existent_version &&
			$existent_version->version_string eq $version->version_string)
		{
			return '';
		} else {
			return sprintf __("unable to re-schedule package '%s'"), $package_name;
		}
	}

	if ((not $self->{_packages}->{$package_name}->version) ||
		($self->{_packages}->{$package_name}->version != $version))
	{
		# binary package names from the same source that supplied package
		my $o_synchronize_source_versions = $self->config->var('cupt::resolver::synchronize-source-versions');

		if ($o_synchronize_source_versions eq 'hard') {
			# need to check is the whole operation doable
			if (!$self->_related_packages_can_be_synchronized($self->{_packages}, $version)) {
				# we cannot do it, do nothing
				return sprintf __('unable to synchronize related binary packages for %s %s'),
						$package_name, $version->version_string;
			}
		}

		# update the requested package
		$self->{_packages}->{$package_name}->version = $version;
		if ($self->config->var('cupt::resolver::track-reasons')) {
			push @{$self->{_packages}->{$package_name}->reasons}, $reason;
		}
		if ($self->config->var('debug::resolver')) {
			my $version_string = $version->version_string;
			mydebug("install package '$package_name', version '$version_string'");
		}
		$self->_schedule_new_version_relations($version);

		if ($o_synchronize_source_versions ne 'none') {
			$self->_synchronize_related_packages($self->{_packages}, $version, 0, \&mydebug);
		}
	}
	return '';
}

sub install_version ($$) {
	my ($self, $version) = @_;
	my $install_error = $self->_install_version_no_stick($version, [ 'user' ]);
	if ($install_error ne '') {
		mydie($install_error);
	}
	$self->{_packages}->{$version->package_name}->stick = 1;
	$self->{_packages}->{$version->package_name}->manually_selected = 1;
	return;
}

sub satisfy_relation_expression ($$) {
	my ($self, $relation_expression) = @_;

	# schedule checking strict relation expression, it will be checked later
	push @{$self->{_strict_satisfy_relation_expressions}}, $relation_expression;
	if ($self->config->var('debug::resolver')) {
		my $message = "strictly satisfying relation '";
		$message .= stringify_relation_expression($relation_expression);
		$message .= "'";
		mydebug($message);
	}
	return;
}

sub unsatisfy_relation_expression ($$) {
	my ($self, $relation_expression) = @_;

	# schedule checking strict relation expression, it will be checked later
	push @{$self->{_strict_unsatisfy_relation_expressions}}, $relation_expression;
	if ($self->config->var('debug::resolver')) {
		my $message = "strictly unsatisfying relation '";
		$message .= stringify_relation_expression($relation_expression);
		$message .= "'";
		mydebug($message);
	}
	return;
}

sub _auto_satisfy_relation ($$) {
	my ($self, $relation_expression, $reason) = @_;

	my $ref_satisfying_versions = $self->cache->get_satisfying_versions($relation_expression);
	if (!__is_version_array_intersects_with_packages($ref_satisfying_versions, $self->{_packages})) {
		# if relation is not satisfied
		if ($self->config->var('debug::resolver')) {
			my $message = "auto-installing relation '";
			$message .= stringify_relation_expression($relation_expression);
			$message .= "'";
			mydebug($message);
		}
		my %pending_relation = (
			'relation_expression' => $relation_expression,
			'reason' => $reason,
		);
		push @{$self->{_pending_relations}}, \%pending_relation;
	}
	return;
}

sub remove_package ($$) {
	my ($self, $package_name) = @_;
	$self->{_packages}->{$package_name} //= Cupt::System::Resolvers::Native::PackageEntry->new();
	$self->{_packages}->{$package_name}->version = undef;
	if ($self->{_packages}->{$package_name}->stick) {
		mydie("unable to re-schedule package '%s'", $package_name);
	}
	$self->{_packages}->{$package_name}->stick = 1;
	$self->{_packages}->{$package_name}->manually_selected = 1;
	if ($self->config->var('cupt::resolver::track-reasons')) {
		push @{$self->{_packages}->{$package_name}->reasons}, [ 'user' ];
	}
	if ($self->config->var('debug::resolver')) {
		mydebug("removing package $package_name");
	}
	return;
}

sub upgrade ($) {
	my ($self) = @_;
	foreach (keys %{$self->{_packages}}) {
		my $package_name = $_;
		my $package = $self->cache->get_binary_package($package_name);
		my $original_version = $self->{_packages}->{$package_name}->version;
		# if there is original version, then at least one policy version should exist
		my $supposed_version = $self->cache->get_policy_version($package);
		defined $supposed_version or
				myinternaldie("supposed version doesn't exist");
		# no need to install the same version
		$original_version->version_string ne $supposed_version->version_string or next;
		$self->_install_version_no_stick($supposed_version, [ 'user' ]);
	}
	return;
}

sub __fair_chooser {
	my ($ref_solution_entries) = @_;

	# choose the solution with maximum score
	return reduce { $a->{'score'} > $b->{'score'} ? $a : $b } @$ref_solution_entries;
}

sub __full_chooser {
	my ($ref_solution_entries) = @_;
	# defer the decision until all solutions are built

	my $ref_unfinished_solution = first { ! $_->{'finished'} } @$ref_solution_entries;

	if (defined $ref_unfinished_solution) {
		return $ref_unfinished_solution;
	} else {
		# heh, whole solution tree has been already built?.. ok, let's choose
		# the best solution
		return __fair_chooser($ref_solution_entries);
	}
}

# every package version has a weight
sub _get_version_weight ($$) {
	my ($self, $version) = @_;

	return 0 if not defined $version;

	my $factor = 1.0;
	my $package_name = $version->package_name;
	if ($version->is_installed() && $self->cache->is_automatically_installed($package_name)) {
		# automatically installed packages count nothing for user
		$factor /= 20.0;
	}
	$factor *= 5.0 if $version->essential;

	# omitting priority 'standard' here
	if ($version->priority eq 'optional') {
		$factor *= 0.9;
	} elsif ($version->priority eq 'extra') {
		$factor *= 0.7;
	} elsif ($version->priority eq 'important') {
		$factor *= 1.4;
	} elsif ($version->priority eq 'required') {
		$factor *= 2.0;
	}
	return $self->cache->get_pin($version) * $factor;
}

sub _get_action_profit ($$$) {
	my ($self, $original_version, $supposed_version) = @_;

	my $result = $self->_get_version_weight($supposed_version) -
   			$self->_get_version_weight($original_version);
	# installing new package
	$result -= 10 if !defined $original_version;
	# remove a package
	$result -= 50 if !defined $supposed_version;

	return $result - $self->config->var('cupt::resolver::quality-bar');
}

sub __is_version_array_intersects_with_packages ($$) {
	my ($ref_versions, $ref_packages) = @_;

	foreach my $version (@$ref_versions) {
		exists $ref_packages->{$version->package_name} or next;

		my $installed_version = $ref_packages->{$version->package_name}->version;
		defined $installed_version or next;

		return 1 if $version->version_string eq $installed_version->version_string;
	}
	return 0;
}

sub _is_package_can_be_removed ($$) {
	my ($self, $package_name) = @_;
	return !$self->config->var('cupt::resolver::no-remove')
			|| !$self->{_packages}->{$package_name}->installed;
}

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

sub _clean_automatically_installed ($) {
	my ($self, $ref_packages) = @_;

	# firstly, prepare all package names that can be potentially removed
	my $can_autoremove = $self->config->var('cupt::resolver::auto-remove');
	my %candidates_for_remove;
	foreach my $package_name (keys %$ref_packages) {
		$package_name ne $_dummy_package_name or next;
		my $package_entry = $ref_packages->{$package_name};
		my $version = $package_entry->version;
		defined $version or next;
		if (exists $self->{_packages}->{$package_name}
			and $self->{_packages}->{$package_name}->manually_selected)
		{
			next;
		}
		# don't consider Essential packages for removal
		$version->essential and next;

		my $can_autoremove_this_package = $can_autoremove ?
				$self->cache->is_automatically_installed($package_name) : 0;
		my $package_was_installed = exists $self->{_old_packages}->{$package_name};
		(not $package_was_installed or $can_autoremove_this_package) or next;

		if (any { $package_name =~ m/$_/ } $self->config->var('apt::neverautoremove')) {
			next;
		}
		# ok, candidate for removing
		$candidates_for_remove{$package_name} = 0;
	}

	my $dependency_graph = Cupt::Graph->new();
	my $main_vertex_package_name = 'main_vertex';
	do { # building dependency graph
		foreach my $package_name (keys %$ref_packages) {
			my $version = $ref_packages->{$package_name}->version;
			defined $version or next;
			my @valuable_relation_expressions;
			push @valuable_relation_expressions, @{$version->pre_depends};
			push @valuable_relation_expressions, @{$version->depends};
			if ($self->config->var('cupt::resolver::keep-recommends')) {
				push @valuable_relation_expressions, @{$version->recommends};
			}
			if ($self->config->var('cupt::resolver::keep-suggests')) {
				push @valuable_relation_expressions, @{$version->suggests};
			}

			foreach (@valuable_relation_expressions) {
				my $satisfying_versions = $self->cache->get_satisfying_versions($_);
				foreach (@$satisfying_versions) {
					my $candidate_package_name = $_->package_name;
					exists $candidates_for_remove{$candidate_package_name} or next;
					my $candidate_version = $ref_packages->{$candidate_package_name}->version;
					$_->version_string eq $candidate_version->version_string or next;
					# well, this is what we need
					if (exists $candidates_for_remove{$package_name}) {
						# this is a relation between two weak packages
						$dependency_graph->add_edge($package_name, $candidate_package_name);
					} else {
						# this is a relation between installed system and a weak package
						$dependency_graph->add_edge($main_vertex_package_name, $candidate_package_name);
					}
					# counting that candidate package was referenced
					++$candidates_for_remove{$candidate_package_name};
				}
			}
		}
	};

	do { # looping the candidates
		# generally speaking, the sane way is to use Graph::TransitiveClosure,
		# but it's sloooow
		my %reachable_vertexes;
		do {
			my @current_vertexes = ($main_vertex_package_name);
			while (scalar @current_vertexes) {
				my $vertex = shift @current_vertexes;
				if (!exists $reachable_vertexes{$vertex}) {
					# ok, new vertex
					$reachable_vertexes{$vertex} = 1;
					push @current_vertexes, $dependency_graph->successors($vertex);
				}
			}
		};
		foreach my $package_name (keys %candidates_for_remove) {
			my $remove;
			if ($candidates_for_remove{$package_name}) {
				# package is in the graph, checking
				$remove = !exists $reachable_vertexes{$package_name};
			} else {
				# remove non-referenced candidates at all
				$remove = 1;
			}
			if ($remove) {
				$ref_packages->{$package_name}->version = undef;
				# leave only one reason :)
				if ($self->config->var('cupt::resolver::track-reasons')) {
					@{$ref_packages->{$package_name}->reasons} = ([ 'auto-remove' ]);
				}
				if ($self->config->var('debug::resolver')) {
					mydebug("auto-removed package '$package_name'");
				}
			}
		}
	};

	# also remove dummy package
	delete $ref_packages->{$_dummy_package_name};

	return;
}

sub _select_solution_chooser ($) {
	my ($self) = @_;

	my %solution_choosers = (
		'fair' => \&__fair_chooser,
		'full' => \&__full_chooser,
	);

	my $resolver_type = $self->config->var('cupt::resolver::type');
	my $sub_solution_chooser = $solution_choosers{$resolver_type};
	defined $sub_solution_chooser or
			mydie("wrong resolver type '%s'", $resolver_type);

	return $sub_solution_chooser;
}

sub _require_strict_relation_expressions ($) {
	my ($self) = @_;

	# "installing" virtual package, which will be used for strict 'satisfy' requests
	my $version = bless [] => 'Cupt::Cache::BinaryVersion';
	$version->package_name = $_dummy_package_name,
	$version->source_package_name = $_dummy_package_name;
	$version->version_string = '';
	$version->pre_depends = [];
	$version->depends = [];
	$version->recommends = [];
	$version->suggests = [];
	$version->breaks = [];
	$version->conflicts = [];

	$self->{_packages}->{$_dummy_package_name} = Cupt::System::Resolvers::Native::PackageEntry->new();
	$self->{_packages}->{$_dummy_package_name}->version = $version;
	$self->{_packages}->{$_dummy_package_name}->stick = 1;
	$self->{_packages}->{$_dummy_package_name}->version->depends =
			$self->{_strict_satisfy_relation_expressions};
	$self->{_packages}->{$_dummy_package_name}->version->breaks =
			$self->{_strict_unsatisfy_relation_expressions};
	return;
}

sub _apply_action ($$$$$) {
	my ($self, $ref_solution_entry, $ref_action_to_apply, $new_solution_identifier, $sub_mydebug_wrapper) = @_;

	my $package_name_to_change = $ref_action_to_apply->{'package_name'};
	my $supposed_version = $ref_action_to_apply->{'version'};

	# stick all requested package names
	foreach my $package_name (@{$ref_action_to_apply->{'package_names_to_stick'}}) {
		$ref_solution_entry->{packages}->{$package_name} //= Cupt::System::Resolvers::Native::PackageEntry->new();
		$ref_solution_entry->{packages}->{$package_name}->stick = 1;
	}

	my $package_entry_to_change = $ref_solution_entry->{'packages'}->{$package_name_to_change};
	my $original_version = $package_entry_to_change->version;

	my $profit = $ref_action_to_apply->{'profit'} //
			$self->_get_action_profit($original_version, $supposed_version);
	$profit *= $ref_action_to_apply->{'factor'};

	if ($self->config->var('debug::resolver')) {
		my $old_version_string = defined($original_version) ?
				$original_version->version_string : '<not installed>';
		my $new_version_string = defined($supposed_version) ?
				$supposed_version->version_string : '<not installed>';

		my $profit_string = $profit;
		$profit_string = "+$profit_string" if $profit > 0;

		my $message = "-> ($new_solution_identifier,$profit_string) " .
				"trying: package '$package_name_to_change': '$old_version_string' -> '$new_version_string'";
		$sub_mydebug_wrapper->($message);
	}

	++$ref_solution_entry->{'level'};
	$ref_solution_entry->{'score'} += $profit;

	$package_entry_to_change->version = $supposed_version;
	if (defined $ref_action_to_apply->{'fakely_satisfies'}) {
		push @{$package_entry_to_change->fake_satisfied}, $ref_action_to_apply->{'fakely_satisfies'};
	}
	if ($self->config->var('cupt::resolver::track-reasons')) {
		if (defined $ref_action_to_apply->{'reason'}) {
			push @{$package_entry_to_change->reasons}, $ref_action_to_apply->{'reason'};
		}
	}
	if ($self->config->var('cupt::resolver::synchronize-source-versions') ne 'none') {
		# dont' do synchronization for removals
		if (defined $supposed_version) {
			$self->_synchronize_related_packages($ref_solution_entry->{'packages'},
					$supposed_version, 1, $sub_mydebug_wrapper);
		}
	}
	return;
}

sub __version_has_relation_expression ($$$) {
	my ($version, $dependency_group_name, $relation_expression) = @_;

	my $relation_string = stringify_relation_expression($relation_expression);
	foreach (@{$version->$dependency_group_name}) {
		if ($relation_string eq stringify_relation_expression($_)) {
			return 1;
		}
	}
	return 0;
}

sub _get_actions_to_fix_dependency ($$$$$$$) { ## no critic (ManyArgs)
	my ($self, $ref_packages, $package_name, $ref_satisfying_versions,
			$relation_expression, $dependency_group_name, $dependency_group_factor) = @_;

	my @result;

	my $version = $ref_packages->{$package_name}->version;

	# install one of versions package needs
	foreach my $satisfying_version (@$ref_satisfying_versions) {
		my $satisfying_package_name = $satisfying_version->package_name;
		# can the package be updated?
		if (!exists $ref_packages->{$satisfying_package_name} ||
			!$ref_packages->{$satisfying_package_name}->stick)
		{
			push @result, {
				'package_name' => $satisfying_package_name,
				'version' => $satisfying_version,
				'factor' => $dependency_group_factor,
				'reason' => [ 'relation expression', $version, $dependency_group_name, $relation_expression ],
			};
		}
	}

	# change this package
	if (!$ref_packages->{$package_name}->stick) {
		# change version of the package
		my $other_package = $self->cache->get_binary_package($package_name);
		foreach my $other_version (@{$other_package->get_versions()}) {
			# don't try existing version
			next if $other_version->version_string eq $version->version_string;

			# let's check if other version has the same relation
			# if it has, other version will also fail so it seems there is no sense trying it
			my $found = __version_has_relation_expression($other_version,
					$dependency_group_name, $relation_expression);
			if (!$found) {
				# let's try harder to find if the other version is really appropriate for us
				foreach (@{$other_version->$dependency_group_name}) {
					# we check only relations from dependency group that caused
					# missing depends, it's not a full check, but pretty reasonable for
					# most cases; in rare cases that some problematic dependency
					# migrated to other dependency group, it will be revealed at
					# next check run

					# fail revealed that no one of available versions of dependent
					# packages can satisfy the main package, so if some relation's
					# satisfying versions are subset of failed ones, the version won't
					# be accepted as a resolution
					my $has_resolution_outside = 0;
					my $ref_candidate_satisfying_versions = $self->cache->get_satisfying_versions($_);
					foreach (@$ref_candidate_satisfying_versions) {
						my $candidate_package_name = $_->package_name;
						my $candidate_version_string = $_->version_string;
						my $is_candidate_appropriate = 1;
						foreach (@$ref_satisfying_versions) {
							next if $_->package_name ne $candidate_package_name;
							next if $_->version_string ne $candidate_version_string;
							# this candidate has fallen into dead-end
							$is_candidate_appropriate = 0;
							last;
						}
						if ($is_candidate_appropriate) {
							# more wide relation, can't say nothing bad with it at time being
							$has_resolution_outside = 1;
							last;
						}
					}
					$found = !$has_resolution_outside;
					last if $found;
				}
				if (!$found) {
					# other version seems to be ok
					push @result, {
						'package_name' => $package_name,
						'version' => $other_version,
						'factor' => $dependency_group_factor,
						'reason' => [ 'relation expression', $version, $dependency_group_name, $relation_expression ],
					};
				}
			}
		}

		if ($self->_is_package_can_be_removed($package_name)) {
			# remove the package
			push @result, {
				'package_name' => $package_name,
				'version' => undef,
				'factor' => $dependency_group_factor,
				'reason' => [ 'relation expression', $version, $dependency_group_name, $relation_expression ],
			};
		}
	}

	return @result;
}

sub __prepare_stick_requests ($) {
	my ($ref_possible_actions) = @_;

	# the each next action receives one more stick request to not interfere with
	# all previous solutions
	my @package_names;
	foreach my $ref_action (@$ref_possible_actions) {
		push @package_names, $ref_action->{'package_name'};
		$ref_action->{'package_names_to_stick'} = [ @package_names ];
	}
	return;
}

sub _resolve ($$) {
	my ($self, $sub_accept) = @_;

	my $sub_solution_chooser = $self->_select_solution_chooser();
	if ($self->config->var('debug::resolver')) {
		mydebug('started resolving');
	}
	$self->_require_strict_relation_expressions();

	# action factor will determine the valuability of action
	# usually it will be 1.0 for strong dependencies and < 1.0 for soft dependencies
	my @dependency_groups;
	push @dependency_groups, { 'name' => 'pre_depends', 'factor' => 2.0, 'target' => 'normal' };
	push @dependency_groups, { 'name' => 'depends', 'factor' => 1.0, 'target' => 'normal' };
	push @dependency_groups, { 'name' => 'conflicts', 'factor' => 1.0, 'target' => 'anti' };
	push @dependency_groups, { 'name' => 'breaks', 'factor' => 1.0, 'target' => 'anti' };
	if ($self->config->var('cupt::resolver::keep-recommends')) {
		push @dependency_groups, { 'name' => 'recommends', 'factor' => 0.4, 'target' => 'normal' };
	}
	if ($self->config->var('cupt::resolver::keep-suggests')) {
		push @dependency_groups, { 'name' => 'suggests', 'factor' => 0.1, 'target' => 'normal' };
	}



	# [ 'packages' => {
	#                   package_name... => {
	#                                        PE_VERSION => version,
	#                                        PE_STICK => boolean
	#                                        PE_FAKE_SATISFIED => [ relation_expression... ]
	#                                        PE_REASONS => [ reason... ]
	#                                        SPE_MANUALLY_SELECTED => boolean
	#                                        SPE_INSTALLED => boolean
	#                                      }
	#                 }
	#   'score' => score
	#   'level' => level
	#   'identifier' => identifier
	#   'finished' => finished (1 | 0)
	# ]...
	my @solutions = ({ packages => __clone_packages($self->{_packages}),
			score => 0, level => 0, identifier => 0, finished => 0 });

	my $next_free_solution_identifier = 1;
	my $ref_current_solution;

	# for each package entry 'count' will contain the number of failures
	# during processing these package
	# { package_name => count }...
	my %failed_counts;

	my $check_failed;

	my $sub_mydebug_wrapper = sub {
		my $level = $ref_current_solution->{'level'};
		my $identifier = $ref_current_solution->{'identifier'};
		my $score_string = sprintf '%.1f', $ref_current_solution->{'score'};
		mydebug(' ' x $level . "($identifier:$score_string) @_");
	};

	my $sub_apply_action = sub {
		my ($ref_solution, $ref_action_to_apply, $new_solution_identifier) = @_;
		$self->_apply_action($ref_solution, $ref_action_to_apply, $new_solution_identifier, $sub_mydebug_wrapper);
	};

	my $return_code;

	do {{
		# will be filled in MAIN_LOOP
		my $package_entry;

		# continue only if we have at least one solution pending, otherwise we have a great fail
		scalar @solutions or do { $return_code = 0; goto EXIT };

		my @possible_actions;

		# choosing the solution to process
		$ref_current_solution = $sub_solution_chooser->(\@solutions);
		my $ref_current_packages = $ref_current_solution->{'packages'};

		# for the speed reasons, we will correct one-solution problems directly in MAIN_LOOP
		# so, when an intermediate problem was solved, maybe it breaks packages
		# we have checked earlier in the loop, so we schedule a recheck
		#
		# once two or more solutions are available, loop will be ended immediately
		my $recheck_needed = 1;
		MAIN_LOOP:
		while ($recheck_needed) {
			$recheck_needed = 0;

			# clearing check_failed
			$check_failed = 0;

			# to speed up the complex decision steps, if solution stack is not
			# empty, firstly check the packages that had a problem
			my @packages_in_order = sort {
				($failed_counts{$b} // 0) <=> ($failed_counts{$a} // 0)
			} keys %$ref_current_packages;

			foreach my $ref_dependency_group (@dependency_groups) {
				my $dependency_group_factor = $ref_dependency_group->{'factor'};
				my $dependency_group_name = $ref_dependency_group->{'name'};
				my $dependency_group_target = $ref_dependency_group->{'target'};

				foreach my $package_name (@packages_in_order) {
					$package_entry = $ref_current_packages->{$package_name};
					my $version = $package_entry->version;
					defined $version or next;

					foreach my $relation_expression (@{$version->$dependency_group_name}) {
						if ($dependency_group_target eq 'normal') {
							# check if relation is already satisfied
							my $ref_satisfying_versions = $self->cache->get_satisfying_versions($relation_expression);
							if (!__is_version_array_intersects_with_packages($ref_satisfying_versions, $ref_current_packages)) {
								if ($dependency_group_name eq 'recommends' or $dependency_group_name eq 'suggests') {
									# this is a soft dependency
									if (!$self->config->var("apt::install-$dependency_group_name")) {
										if (!__is_version_array_intersects_with_packages(
												$ref_satisfying_versions, $self->{_old_packages}))
										{
											# it wasn't satisfied in the past, don't touch it
											next;
										}
									}
									if (exists $self->{_old_packages}->{$version->package_name}) {
										my $old_version = $self->{_old_packages}->{$version->package_name}->version;
										if (defined $old_version and __version_has_relation_expression($old_version,
											$dependency_group_name, $relation_expression))
										{
											# the fact that we are here means that the old version of this package
											# had exactly the same relation expression, and it was unsatisfied
											# so, upgrading the version doesn't bring anything new
											next;
										}
									}
									if (any { $_ == $relation_expression } @{$package_entry->fake_satisfied}) {
										# this soft relation expression was already fakely satisfied (score penalty)
										next;
									}
									# ok, then we have one more possible solution - do nothing at all
									push @possible_actions, {
										'package_name' => $package_name,
										'version' => $version,
										'factor' => $dependency_group_factor,
										# set profit manually, as we are inserting fake action here
										'profit' => -50,
										'fakely_satisfies' => $relation_expression,
										'reason' => undef,
									};
								}
								# mark package as failed one more time
								++$failed_counts{$package_name};

								# for resolving we can do:
								push @possible_actions, $self->_get_actions_to_fix_dependency(
										$ref_current_packages, $package_name, $ref_satisfying_versions,
										$relation_expression, $dependency_group_name, $dependency_group_factor);

								__prepare_stick_requests(\@possible_actions);

								if ($self->config->var('debug::resolver')) {
									my $stringified_relation = stringify_relation_expression($relation_expression);
									$sub_mydebug_wrapper->("problem: package '$package_name': " .
											"unsatisfied $dependency_group_name '$stringified_relation'");
								}
								$check_failed = 1;

								if (scalar @possible_actions == 1) {
									$sub_apply_action->($ref_current_solution,
											$possible_actions[0], $ref_current_solution->{'identifier'});
									@possible_actions = ();
									$recheck_needed = 1;
									next MAIN_LOOP;
								}
								$recheck_needed = 0;
								last MAIN_LOOP;
							}
						} else {
							# check if relation is accidentally satisfied
							my $ref_satisfying_versions = $self->cache->get_satisfying_versions($relation_expression);
							if (__is_version_array_intersects_with_packages($ref_satisfying_versions, $ref_current_packages)) {
								# so, this can conflict... check it deeper on the fly
								my $conflict_found = 0;
								foreach my $satisfying_version (@$ref_satisfying_versions) {
									my $other_package_name = $satisfying_version->package_name;

									# package can't conflict (or break) with itself
									$other_package_name ne $package_name or next;

									# is the package installed?
									exists $ref_current_packages->{$other_package_name} or next;

									my $other_package_entry = $ref_current_packages->{$other_package_name};

									# does the package have an installed version?
									defined($other_package_entry->version) or next;

									# is this our version?
									$other_package_entry->version->version_string eq $satisfying_version->version_string or next;

									# :(
									$conflict_found = 1;

									# additionally, in case of absense of stick, also contribute to possible actions
									if (!$other_package_entry->stick) {
										# so change it
										my $other_package = $self->cache->get_binary_package($other_package_name);
										foreach my $other_version (@{$other_package->get_versions()}) {
											# don't try existing version
											next if $other_version->version_string eq $satisfying_version->version_string;

											push @possible_actions, {
												'package_name' => $other_package_name,
												'version' => $other_version,
												'factor' => $dependency_group_factor,
												'reason' => [ 'relation expression', $version, $dependency_group_name, $relation_expression ],
											};
										}

										if ($self->_is_package_can_be_removed($other_package_name)) {
											# or remove it
											push @possible_actions, {
												'package_name' => $other_package_name,
												'version' => undef,
												'factor' => $dependency_group_factor,
												'reason' => [ 'relation expression', $version, $dependency_group_name, $relation_expression ],
											};
										}
									}
								}

								if ($conflict_found) {
									$check_failed = 1;

									# mark package as failed one more time
									++$failed_counts{$package_name};

									if (!$package_entry->stick) {
										# change version of the package
										my $package = $self->cache->get_binary_package($package_name);
										foreach my $other_version (@{$package->get_versions()}) {
											# don't try existing version
											next if $other_version->version_string eq $version->version_string;

											push @possible_actions, {
												'package_name' => $package_name,
												'version' => $other_version,
												'factor' => $dependency_group_factor,
												'reason' => [ 'relation expression', $version, $dependency_group_name, $relation_expression ],
											};
										}

										if ($self->_is_package_can_be_removed($package_name)) {
											# remove the package
											push @possible_actions, {
												'package_name' => $package_name,
												'version' => undef,
												'factor' => $dependency_group_factor,
												'reason' => [ 'relation expression', $version, $dependency_group_name, $relation_expression ],
											};
										}
									}

									__prepare_stick_requests(\@possible_actions);

									if ($self->config->var('debug::resolver')) {
										my $stringified_relation = stringify_relation_expression($relation_expression);
										$sub_mydebug_wrapper->("problem: package '$package_name': " .
												"satisfied $dependency_group_name '$stringified_relation'");
									}
									$recheck_needed = 0;
									last MAIN_LOOP;
								}
							}
						}
					}
				}
			}
		}

		if (!$check_failed) {
			# in case we go next
			$check_failed = 1;

			# if the solution was only just finished
			if ($self->config->var('debug::resolver') && !$ref_current_solution->{'finished'}) {
				$sub_mydebug_wrapper->('finished');
				$ref_current_solution->{'finished'} = 1;
			}
			# resolver can refuse the solution
			my $ref_new_selected_solution = $sub_solution_chooser->(\@solutions);

			if ($ref_new_selected_solution ne $ref_current_solution) {
				# ok, process other solution
				next;
			}

			# clean up automatically installed by resolver and now unneeded packages
			$self->_clean_automatically_installed($ref_current_packages);

			# build "user-frienly" version of solution
			my %suggested_packages;
			foreach my $package_name (keys %$ref_current_packages) {
				my $other_package_entry = $ref_current_packages->{$package_name};
				$suggested_packages{$package_name}->{'version'} = $other_package_entry->version;
				$suggested_packages{$package_name}->{'reasons'} = $other_package_entry->reasons;
				$suggested_packages{$package_name}->{'manually_selected'} =
						(exists $self->{_packages}->{$package_name} and
						$self->{_packages}->{$package_name}->manually_selected);
			}

			# suggest found solution
			if ($self->config->var('debug::resolver')) {
				$sub_mydebug_wrapper->('proposing this solution');
			}
			my $user_answer = $sub_accept->(\%suggested_packages);
			if (!defined $user_answer) {
				# user has selected abandoning all further efforts
				goto EXIT;
			} elsif ($user_answer) {
				# yeah, this is end of our tortures
				if ($self->config->var('debug::resolver')) {
					$sub_mydebug_wrapper->('accepted');
				}
				$return_code = 1;
				goto EXIT;
			} else {
				# caller hasn't accepted this solution, well, go next...
				if ($self->config->var('debug::resolver')) {
					$sub_mydebug_wrapper->('declined');
				}
				# purge current solution
				@solutions = grep { $_ ne $ref_current_solution } @solutions;
				next;
			}
		}

		if ($self->config->var('cupt::resolver::synchronize-source-versions') eq 'hard') {
			# if we have to synchronize source versions, can related packages be updated too?
			# filter out actions that don't match this criteria
			my @new_possible_actions;
			foreach my $possible_action (@possible_actions) {
				my $version = $possible_action->{'version'};
				if (not defined $version or
					$self->_related_packages_can_be_synchronized($ref_current_packages, $version))
				{
					push @new_possible_actions, $possible_action;
				} else {
					# we cannot proceed with it
					if ($self->config->var('debug::resolver')) {
						$sub_mydebug_wrapper->(sprintf(
								'cannot consider installing %s %s: unable to synchronize related binary packages',
								$version->package_name, $version->version_string));
					}
				}
			}
			@possible_actions = @new_possible_actions;
		}

		if (scalar @possible_actions) {
			# firstly rank all solutions
			my $position_penalty = 0;
			foreach (@possible_actions) {
				my $package_name = $_->{'package_name'};
				my $supposed_version = $_->{'version'};
				my $original_version = exists $ref_current_packages->{$package_name} ?
						$ref_current_packages->{$package_name}->version : undef;

				$_->{'profit'} //= $self->_get_action_profit($original_version, $supposed_version);
				$_->{'profit'} -= $position_penalty;
				++$position_penalty;
			}

			# sort them by "rank", from more good to more bad
			@possible_actions = sort { $b->{profit} <=> $a->{profit} } @possible_actions;

			my @forked_solutions;
			# fork the solution entry and apply all the solutions by one
			foreach my $idx (0..$#possible_actions) {
				my $ref_cloned_solution;
				if ($idx == $#possible_actions) {
					# use existing solution entry
					$ref_cloned_solution = $ref_current_solution;
				} else {
					# clone the current stack to form a new one
					# we can obviously use Storable::dclone, or Clone::Clone here, but speed...
					$ref_cloned_solution = {
						packages => __clone_packages($ref_current_solution->{'packages'}),
						level => $ref_current_solution->{'level'},
						score => $ref_current_solution->{'score'},
						finished => 0,
					};
					push @forked_solutions, $ref_cloned_solution;
				}

				# apply the solution
				my $ref_action_to_apply = $possible_actions[$idx];
				my $new_solution_identifier = $next_free_solution_identifier++;
				$sub_apply_action->($ref_cloned_solution, $ref_action_to_apply, $new_solution_identifier);
				$ref_cloned_solution->{identifier} = $new_solution_identifier;
			}

			# adding forked solutions to main solution storage
			push @solutions, @forked_solutions;

			# don't allow solution tree to grow unstoppably
			while (scalar @solutions > $self->config->var('cupt::resolver::max-solution-count')) {
				# find the worst solution and drop it
				my $ref_worst_solution = reduce { $a->{'score'} < $b->{'score'} ? $a : $b } @solutions;
				# temporary setting current solution to worst
				$ref_current_solution = $ref_worst_solution;
				if ($self->config->var('debug::resolver')) {
					$sub_mydebug_wrapper->('dropping this solution');
				}
				@solutions = grep { $_ ne $ref_current_solution } @solutions;
			}
		} else {
			if ($self->config->var('debug::resolver')) {
				$sub_mydebug_wrapper->('no solutions');
			}
			# purge current solution
			@solutions = grep { $_ ne $ref_current_solution } @solutions;
		}
	}} while $check_failed;

	EXIT:
	delete $self->{_packages}->{$_dummy_package_name};
	return $return_code;
}

sub resolve ($$) {
	my ($self, $sub_accept) = @_;

	# unwinding relations
	while (scalar @{$self->{_pending_relations}}) {
		my $ref_pending_relation = shift @{$self->{_pending_relations}};
		my $relation_expression = $ref_pending_relation->{'relation_expression'};
		my $ref_satisfying_versions = $self->cache->get_satisfying_versions($relation_expression);

		# if we have no candidates, skip the relation
		scalar @$ref_satisfying_versions or next;

		# installing most preferrable version

		my $version_to_install = $ref_satisfying_versions->[0];
		if ($self->config->var('debug::resolver')) {
			mydebug("selected package '%s', version '%s' for relation expression '%s'",
					$version_to_install->package_name,
					$version_to_install->version_string,
					stringify_relation_expression($relation_expression)
			);
		}
		my $reason = $ref_pending_relation->{'reason'};
		$self->_install_version_no_stick($version_to_install, $reason);
		# note that _install_version_no_stick can add some pending relations
	}

	# throw away all obsoleted packages at this stage to avoid resolver from
	# ever bothering of them
	$self->_clean_automatically_installed($self->{_packages});

	# at this stage we have all extraneous dependencies installed, now we should check inter-depends
	return $self->_resolve($sub_accept);
}

1;

