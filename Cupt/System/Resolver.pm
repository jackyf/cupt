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
package Cupt::System::Resolver;

=head1 NAME

Cupt::System::Resolver - dependency problem resolver for Cupt

=cut

use 5.10.0;
use strict;
use warnings;

use Cupt::Core;
use Cupt::Cache::Relation qw(stringify_relation_expression);

use Graph;
use constant {
	PE_VERSION => 0,
	PE_STICK => 1,
	PE_FAKE_SATISFIED => 2,
	SPE_MANUALLY_SELECTED => 3,
	SPE_INSTALLED => 4,
};

our $_dummy_package_name = "dummy_package_name";

=begin internal

=head2 _pending_relations

array of relations which are to be satisfied by final resolver, used for
filling depends, recommends (optionally), suggests (optionally) of requested
packages, or for satisfying some requested relations

=end internal

=cut

use fields qw(_config _cache _params _old_packages _packages _pending_relations
		_strict_relation_expressions);

=head1 PARAMS

parameters that change resolver's behaviour, can be set by L</set_params> method

=head2 resolver-type

see L<cupt manual|cupt/--resolver=>

=head2 max-solution-count

see L<cupt manual|cupt/--max-resolver-count=>

=head1 METHODS

=head2 new

creates new Cupt::System::Resolver object

Parameters: 

I<config> - reference to L<Cupt::Config|Cupt::Config>

I<cache> - reference to L<Cupt::Cache|Cupt::Cache>

=cut

sub new {
	my $class = shift;
	my $self = fields::new($class);

	# common apt config
	$self->{_config} = shift;

	$self->{_cache} = shift;

	# resolver params
	%{$self->{_params}} = (
		'resolver-type' => 'multiline-fair',
		'max-solution-count' => 256,
	);

	$self->{_pending_relations} = [];
	$self->{_strict_relation_expressions} = [];

	return $self;
}

=head2 set_params

method, sets params for the resolver

Parameters: hash (as list) of params and their values

Example: C<< $resolver->set_params('no-remove' => 1); >>

=cut

sub set_params {
	my ($self) = shift;
	while (@_) {
		my $key = shift;
		my $value = shift;
		$self->{_params}->{$key} = $value;
	}
}

sub _create_new_package_entry ($$) {
	my ($self, $package_name) = @_;
	return if exists $self->{_packages}->{$package_name};
	my ($package_entry) = ($self->{_packages}->{$package_name} = []);
	$package_entry->[PE_VERSION] = undef;
	$package_entry->[PE_STICK] = 0;
	$package_entry->[PE_FAKE_SATISFIED] = [];
	$package_entry->[SPE_MANUALLY_SELECTED] = 0;
	$package_entry->[SPE_INSTALLED] = 0;
}

=head2 import_installed_versions

method, imports already installed versions, usually used in pair with
L<&Cupt::System::State::export_installed_versions|Cupt::System::State/export_installed_versions>

Parameters:

I<ref_versions> - reference to array of L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion>

=cut

sub import_installed_versions ($$) {
	my ($self, $ref_versions) = @_;

	foreach my $version (@$ref_versions) {
		# just moving versions to packages, don't try install or remove some dependencies
		# '_packages' will be modified, leave '_old_packages' as original system state
		$self->_create_new_package_entry($version->{package_name});
		$self->{_packages}->{$version->{package_name}}->[PE_VERSION] = $version;
		$self->{_packages}->{$version->{package_name}}->[SPE_INSTALLED] = 1;
		@{$self->{_old_packages}->{$version->{package_name}}} = @{$self->{_packages}->{$version->{package_name}}};
	}
}

sub _schedule_new_version_relations ($$) {
	my ($self, $version) = @_;

	# unconditionally adding pre-depends
	foreach (@{$version->{pre_depends}}) {
		$self->_auto_satisfy_relation($_);
	}
	# unconditionally adding depends
	foreach (@{$version->{depends}}) {
		$self->_auto_satisfy_relation($_);
	}
	if ($self->{_config}->var('apt::install-recommends')) {
		# ok, so adding recommends
		foreach (@{$version->{recommends}}) {
			$self->_auto_satisfy_relation($_);
		}
	}
	if ($self->{_config}->var('apt::install-suggests')) {
		# ok, so adding suggests
		foreach (@{$version->{suggests}}) {
			$self->_auto_satisfy_relation($_);
		}
	}
}

# installs new version, shedules new dependencies, but not sticks it
sub _install_version_no_stick ($$) {
	my ($self, $version) = @_;
	$self->_create_new_package_entry($version->{package_name});
	if ($self->{_packages}->{$version->{package_name}}->[PE_STICK])
	{
		# package is restricted to be updated
		return;
	}

	$self->{_packages}->{$version->{package_name}}->[PE_VERSION] = $version;
	if ($self->{_config}->var('debug::resolver')) {
		mydebug("install package '$version->{package_name}', version '$version->{version_string}'");
	}
	$self->_schedule_new_version_relations($version);
}

=head2 install_version

method, installs a new version with requested dependencies

Parameters:

I<version> - reference to L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion>

=cut

sub install_version ($$) {
	my ($self, $version) = @_;
	$self->_install_version_no_stick($version);
	$self->{_packages}->{$version->{package_name}}->[PE_STICK] = 1;
	$self->{_packages}->{$version->{package_name}}->[SPE_MANUALLY_SELECTED] = 1;
}

=head2 satisfy_relation

method, installs all needed versions to satisfy L<relation expression|Cupt::Cache::Relation/Relation expression>

Parameters:

I<relation_expression> - see L<Cupt::Cache::Relation/Relation expression>

=cut

sub satisfy_relation_expression ($$) {
	my ($self, $relation_expression) = @_;

	# schedule checking strict relation expression, it will be checked later
	push @{$self->{_strict_relation_expressions}}, $relation_expression;
}

sub _auto_satisfy_relation ($$) {
	my ($self, $relation_expression) = @_;

	my $ref_satisfying_versions = $self->{_cache}->get_satisfying_versions($relation_expression);
	if (!__is_version_array_intersects_with_packages($ref_satisfying_versions, $self->{_packages})) {
		# if relation is not satisfied
		if ($self->{_config}->var('debug::resolver')) {
			my $message = "auto-installing relation '";
			$message .= stringify_relation_expression($relation_expression);
			$message .= "'";
			mydebug($message);
		}
		push @{$self->{_pending_relations}}, $relation_expression;
	}
}

=head2 remove_package

method, removes a package

Parameters:

I<package_name> - string, name of package to remove

=cut

sub remove_package ($$) {
	my ($self, $package_name) = @_;
	$self->_create_new_package_entry($package_name);
	$self->{_packages}->{$package_name}->[PE_VERSION] = undef;
	$self->{_packages}->{$package_name}->[PE_STICK] = 1;
	$self->{_packages}->{$package_name}->[SPE_MANUALLY_SELECTED] = 1;
	if ($self->{_config}->var('debug::resolver')) {
		mydebug("removing package $package_name");
	}
}

=head2 upgrade

method, schedule upgrade of as much packages in system as possible

=cut

sub upgrade ($) {
	my ($self) = @_;
	foreach (keys %{$self->{_packages}}) {
		my $package_name = $_;
		my $package = $self->{_cache}->get_binary_package($package_name);
		my $original_version = $self->{_packages}->{$package_name}->[PE_VERSION];
		my $supposed_version = $self->{_cache}->get_policy_version($package);
		# no need to install the same version
		$original_version->{version_string} ne $supposed_version->{version_string} or next;
		$self->_install_version_no_stick($supposed_version);
	}
}

sub __normalized_score ($) {
	my ($ref_solution_entry) = @_;
	return $ref_solution_entry->{'score'} / sqrt($ref_solution_entry->{'level'} + 1);
}

sub __first_good_chooser {
	return 0;
}

sub __multiline_fair_chooser {
	my ($ref_solution_entries) = @_;

	my $max_normalized_score = __normalized_score($ref_solution_entries->[0]);
	my $idx_of_max = 0;
	foreach my $idx (1..$#{$ref_solution_entries}) {
		my $current_normalized_score = __normalized_score($ref_solution_entries->[$idx]);
		if ($max_normalized_score < $current_normalized_score) {
			$max_normalized_score = $current_normalized_score;
			$idx_of_max = $idx;
		}
	}
	return $idx_of_max;
}

sub __multiline_full_chooser {
	my ($ref_solution_entries) = @_;
	# defer the decision until all solutions are built
	foreach my $idx ($#{$ref_solution_entries}) {
		if (! $ref_solution_entries->[$idx]->{finished}) {
			# process it
			return $idx;
		}
	}

	# what?! all tree has been already built?.. ok, let's choose the best
	# solution
	return __multiline_fair_chooser($ref_solution_entries);
}

# every package version has a weight
sub _get_version_weight ($$) {
	my ($self, $version) = @_;
	return 0 if !defined $version;
	my $package_name = $version->{package_name};
	if ($version->is_installed() && $self->{_cache}->is_automatically_installed($package_name)) {
		# automatically installed packages count nothing for user
		return 0;
	}
	my $result = $self->{_cache}->get_pin($version);
	$result += 5000 if defined($version->{essential});
	$result += 2000 if $version->{priority} eq 'required';
	$result += 1000 if $version->{priority} eq 'important';
	$result += 400 if $version->{priority} eq 'standard';
	$result += 100 if $version->{priority} eq 'optional';
}

sub _get_action_profit ($$$) {
	my ($self, $original_version, $supposed_version) = @_;

	# if the package was not installed there is no any profit of installing it,
	# all packages user want are either installed of selected manually
	#
	# to limit installing new packages, give it small negative profit
	return -100 if !defined $original_version;

	# ok, just return difference in weights
	return $self->_get_version_weight($supposed_version) - $self->_get_version_weight($original_version);
}

sub __is_version_array_intersects_with_packages ($$) {
	my ($ref_versions, $ref_packages) = @_;

	foreach my $version (@$ref_versions) {
		exists $ref_packages->{$version->{package_name}} or next;

		my $installed_version = $ref_packages->{$version->{package_name}}->[PE_VERSION];
		defined $installed_version or next;
		
		return 1 if $version->{version_string} eq $installed_version->{version_string};
	}
	return 0;
}

sub _is_package_can_be_removed ($$) {
	my ($self, $package_name) = @_;
	return !$self->{_config}->var('cupt::resolver::no-remove')
			|| !$self->{_packages}->{$package_name}->[SPE_INSTALLED];
}

sub _get_dependencies_groups ($$) {
	my ($self, $version) = @_;

	# action koeficient will determine the valuability of action
	# usually it will be 1.0 for strong dependencies and < 1.0 for soft dependencies
	my @result;
	push @result, { 'name' => 'pre-depends', 'relation_expressions' => $version->{pre_depends}, 'koef' => 2.0 };
	push @result, { 'name' => 'depends', 'relation_expressions' => $version->{depends}, 'koef' => 1.0 };
	if ($self->{_config}->var('cupt::resolver::keep-recommends')) {
		push @result, { 'name' => 'recommends', 'relation_expressions' => $version->{recommends}, 'koef' => 0.4 };
	}
	if ($self->{_config}->var('cupt::resolver::keep-suggests')) {
		push @result, { 'name' => 'suggests', 'relation_expressions' => $version->{suggests}, 'koef' => 0.1 };
	}

	return \@result;
}

sub __clone_packages ($) {
	my ($ref_packages) = @_;

	my %clone;
	foreach (keys %$ref_packages) {
		my $ref_new_package_entry = $ref_packages->{$_};
		$clone{$_}->[PE_VERSION] = $ref_new_package_entry->[PE_VERSION];
		$clone{$_}->[PE_STICK] = $ref_new_package_entry->[PE_STICK];
		$clone{$_}->[PE_FAKE_SATISFIED] = $ref_new_package_entry->[PE_FAKE_SATISFIED];
	}
	return \%clone;
}

sub _clean_automatically_installed ($) {
	my ($self, $ref_packages) = @_;

	# firstly, prepare all package names that can be potentially removed
	my $can_autoremove = $self->{_config}->var('cupt::resolver::auto-remove');
	my %candidates_for_remove;
	foreach my $package_name (keys %$ref_packages) {
		my $ref_package_entry = $ref_packages->{$package_name};
		my $version = $ref_package_entry->[PE_VERSION];
		defined $version or next;
		!$ref_package_entry->[PE_STICK] or next;
		!$self->{_packages}->{$package_name}->[SPE_MANUALLY_SELECTED] or next;

		my $can_autoremove_this_package = $can_autoremove ?
				$self->{_cache}->is_automatically_installed($package_name) : 0;
		my $package_was_installed = exists $self->{_old_packages}->{$package_name};
		(!$package_was_installed or $can_autoremove_this_package) or next;

		grep { $package_name eq $_ } $self->{_config}->var('apt::neverautoremove') and next;
		# ok, candidate for removing
		$candidates_for_remove{$package_name} = 0;
	}

	my $dependency_graph = new Graph('directed' => 1);
	my $main_vertex_package_name = "main_vertex";
	do { # building dependency graph
		foreach my $package_name (keys %$ref_packages) {
			my $version = $ref_packages->{$package_name}->[PE_VERSION];
			defined $version or next;
			my @valuable_relation_expressions;
			push @valuable_relation_expressions, @{$version->{pre_depends}};
			push @valuable_relation_expressions, @{$version->{depends}};
			if ($self->{_config}->var('cupt::resolver::keep-recommends')) {
				push @valuable_relation_expressions, @{$version->{recommends}};
			}
			if ($self->{_config}->var('cupt::resolver::keep-suggests')) {
				push @valuable_relation_expressions, @{$version->{suggests}};
			}

			foreach (@valuable_relation_expressions) {
				my $satisfying_versions = $self->{_cache}->get_satisfying_versions($_);
				foreach (@$satisfying_versions) {
					my $candidate_package_name = $_->{package_name};
					exists $candidates_for_remove{$candidate_package_name} or next;
					my $candidate_version = $ref_packages->{$candidate_package_name}->[PE_VERSION];
					$_->{version_string} eq $candidate_version->{version_string} or next;
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
		while (my $package_name = each %candidates_for_remove) {
			my $remove;
			if ($candidates_for_remove{$package_name}) {
				# package is in the graph, checking
				$remove = !exists $reachable_vertexes{$package_name};
			} else {
				# remove non-referenced candidates at all
				$remove = 1;
			}
			$ref_packages->{$package_name}->[PE_VERSION] = undef if $remove;
		}
	};

	# also remove dummy package
	delete $ref_packages->{$_dummy_package_name};
}

sub _resolve ($$) {
	my ($self, $sub_accept) = @_;

	my %solution_choosers = (
		'first-good' => \&__first_good_chooser,
		'multiline-fair' => \&__multiline_fair_chooser,
		'multiline-full' => \&__multiline_full_chooser,
	);

	my $sub_solution_chooser = $solution_choosers{$self->{_params}->{'resolver-type'}};

	if ($self->{_config}->var('debug::resolver')) {
		mydebug("started resolving");
	}

	# "installing" virtual package, which will be used for strict 'satisfy' requests
	my $version = {
		package_name => $_dummy_package_name,
		pre_depends => [],
		depends => [],
		recommends => [],
		suggests => [],
		breaks => [],
		conflicts => [],
	};
	$self->_create_new_package_entry($_dummy_package_name);
	$self->{_packages}->{$_dummy_package_name}->[PE_VERSION] = $version;
	$self->{_packages}->{$_dummy_package_name}->[PE_STICK] = 1;
	$self->{_packages}->{$_dummy_package_name}->[PE_VERSION]->{depends} = 
			$self->{_strict_relation_expressions};

	# [ 'packages' => {
	#                   package_name... => {
	#                                        PE_VERSION => version,
	#                                        PE_STICK => boolean
	#                                        PE_FAKE_SATISFIED => [ relation_expression... ]
	#                                        SPE_MANUALLY_SELECTED => boolean
	#                                        SPE_INSTALLED => boolean
	#                                      }
	#                 }
	#   'score' => score
	#   'level' => level
	#   'identifier' => identifier
	#   'finished' => finished (1 | 0)
	# ]...
	my @solution_entries = ({ packages => __clone_packages($self->{_packages}),
			score => 0, level => 0, identifier => 0, finished => 0 });

	my $next_free_solution_identifier = 1;
	my $selected_solution_entry_index;

	# for each package entry 'count' will contain the number of failures
	# during processing these package
	# { package_name => count }...
	my %failed_counts;

	my $check_failed;

	# will be filled in MAIN_LOOP
	my $package_entry;
	my $package_name;

	my $sub_mydebug_wrapper = sub {
		my $ref_solution_entry = $solution_entries[$selected_solution_entry_index];
		my $level = $ref_solution_entry->{level};
		my $identifier = $ref_solution_entry->{identifier};
		my $normalized_score_string = sprintf "%.1f", __normalized_score($ref_solution_entry);
		mydebug(" " x $level . "($identifier:$normalized_score_string) @_");
	};


	my $sub_apply_action = sub {
		my ($ref_solution_entry, $ref_action_to_apply, $new_solution_identifier) = @_;

		my $package_name_to_change = $ref_action_to_apply->{package_name};
		my $supposed_version = $ref_action_to_apply->{version};

		my $ref_package_entry_to_change = $ref_solution_entry->{packages}->{$package_name_to_change};
		my $original_version = $ref_package_entry_to_change->[PE_VERSION];

		my $profit = $ref_action_to_apply->{profit} //
				$self->_get_action_profit($original_version, $supposed_version);
		$profit *= $ref_action_to_apply->{koef};

		if ($self->{_config}->var('debug::resolver')) {
			my $old_version_string = defined($original_version) ?
					$original_version->{version_string} : '<not installed>';
			my $new_version_string = defined($supposed_version) ?
					$supposed_version->{version_string} : '<not installed>';

			my $profit_string = $profit;
			$profit_string = "+$profit_string" if $profit > 0;

			my $message = "-> ($new_solution_identifier,$profit_string) " .
					"trying: package '$package_name_to_change': '$old_version_string' -> '$new_version_string'";
			$sub_mydebug_wrapper->($message);
		}

		# raise the level
		++$ref_solution_entry->{level};

		$ref_solution_entry->{score} += $profit;

		# set stick for change for the time on underlying solutions
		$ref_package_entry_to_change->[PE_STICK] = 1;
		$ref_package_entry_to_change->[PE_VERSION] = $supposed_version;
		if (exists $ref_action_to_apply->{fakely_satisfies}) {
			push @{$ref_package_entry_to_change->[PE_FAKE_SATISFIED]}, $ref_action_to_apply->{fakely_satisfies};
		}
	};

	my $return_code;

	do {{
		# continue only if we have at least one solution pending, otherwise we have a great fail
		scalar @solution_entries or do { $return_code = 0; goto EXIT };

		my @possible_actions;

		# choosing the solution entry to process
		$selected_solution_entry_index = $sub_solution_chooser->(\@solution_entries);

		my $ref_current_solution_entry = $solution_entries[$selected_solution_entry_index];

		my $ref_current_packages = $ref_current_solution_entry->{packages};

		my $package_name;

		# for the speed reasons, we will correct one-solution problems directly in MAIN_LOOP
		# so, when an intermediate problem was solved, maybe it breaks packages
		# we have checked earlier in the loop, so we schedule a recheck
		#
		# once two or more solutions are available, loop will be ended immediately
		my $recheck_needed = 1;
		while ($recheck_needed) {
			$recheck_needed = 0;

			# clearing check_failed
			$check_failed = 0;

			# to speed up the complex decision steps, if solution stack is not
			# empty, firstly check the packages that had a problem
			my @packages_in_order = sort {
				($failed_counts{$b} // 0) <=> ($failed_counts{$a} // 0)
			} keys %$ref_current_packages;

			MAIN_LOOP:
			foreach (@packages_in_order) {
				$package_name = $_;
				$package_entry = $ref_current_packages->{$package_name};
				my $version = $package_entry->[PE_VERSION];
				defined $version or next;

				# checking that all dependencies are satisfied
				foreach my $ref_dependency_group (@{$self->_get_dependencies_groups($version)}) {
					my $dependency_group_koef = $ref_dependency_group->{koef};
					my $dependency_group_name = $ref_dependency_group->{name};
					foreach my $relation_expression (@{$ref_dependency_group->{relation_expressions}}) {
						# check if relation is already satisfied
						my $ref_satisfying_versions = $self->{_cache}->get_satisfying_versions($relation_expression);
						if (__is_version_array_intersects_with_packages($ref_satisfying_versions, $ref_current_packages)) {
							# good, nothing to do
						} else {
							if ($dependency_group_name eq 'recommends' or $dependency_group_name eq 'suggests') {
								# this is a soft dependency
								if (!__is_version_array_intersects_with_packages(
										$ref_satisfying_versions, $self->{_old_packages}))
								{
									# it wasn't satisfied in the past, don't touch it
									next;
								} elsif (grep { $_ == $relation_expression } @{$package_entry->[PE_FAKE_SATISFIED]}) {
									# this soft relation expression was already fakely satisfied (score penalty)
									next;
								} else {
									# ok, then we have one more possible solution - do nothing at all
									push @possible_actions, {
										'package_name' => $package_name,
										'version' => $version,
										'koef' => $dependency_group_koef,
										# set profit manually, as we are inserting fake action here
										'profit' => -50,
										'fakely_satisfies' => $relation_expression,
									};
								}
							}
							# mark package as failed one more time
							++$failed_counts{$package_name};

							# for resolving we can do:

							# install one of versions package needs
							foreach my $satisfying_version (@$ref_satisfying_versions) {
								if (!$ref_current_packages->{$satisfying_version->{package_name}}->[PE_STICK]) {
									push @possible_actions, {
										'package_name' => $satisfying_version->{package_name},
										'version' => $satisfying_version,
										'koef' => $dependency_group_koef,
									};
								}
							}

							# change this package
							if (!$package_entry->[PE_STICK]) {
								# change version of the package
								my $other_package = $self->{_cache}->get_binary_package($package_name);
								foreach my $other_version (@{$other_package->versions()}) {
									# don't try existing version
									next if $other_version->{version_string} eq $version->{version_string};

									# let's check if other version has the same relation
									my $failed_relation_string = stringify_relation_expression($relation_expression);
									my $found = 0;
									foreach (@{$other_version->{$dependency_group_name}}) {
										if ($failed_relation_string eq stringify_relation_expression($_)) {
											# yes, it has the same relation expression, so other version will
											# also fail so it seems there is no sense trying it
											$found = 1;
											last;
										}
									}
									if (!$found) {
										# let's try harder to find if the other version is really appropriate for us
										foreach (@{$other_version->{$dependency_group_name}}) {
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
											my $ref_candidate_satisfying_versions = $self->{_cache}->get_satisfying_versions($_);
											foreach (@$ref_candidate_satisfying_versions) {
												my $candidate_package_name = $_->{package_name};
												my $candidate_version_string = $_->{version_string};
												my $is_candidate_appropriate = 1;
												foreach (@$ref_satisfying_versions) {
													next if $_->{package_name} ne $candidate_package_name;
													next if $_->{version_string} ne $candidate_version_string;
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
											push @possible_actions, {
												'package_name' => $package_name,
												'version' => $other_version,
												'koef' => $dependency_group_koef
											};
										}
									}
								}

								if ($self->_is_package_can_be_removed($package_name)) {
									# remove the package
									push @possible_actions, {
										'package_name' => $package_name,
										'version' => undef,
										'koef' => $dependency_group_koef
									};
								}
							}

							# in any case, stick this package
							$package_entry->[PE_STICK] = 1;

							if ($self->{_config}->var('debug::resolver')) {
								my $stringified_relation = stringify_relation_expression($relation_expression);
								$sub_mydebug_wrapper->("problem: package '$package_name': " . 
										"unsatisfied $dependency_group_name '$stringified_relation'");
							}
							$check_failed = 1;

							if (scalar @possible_actions == 1) {
								$sub_apply_action->($ref_current_solution_entry,
										$possible_actions[0], $ref_current_solution_entry->{identifier});
								@possible_actions = ();
								$recheck_needed = 1;
								next MAIN_LOOP;
							}
							$recheck_needed = 0;
							last MAIN_LOOP;
						}
					}
				}

				# checking that all 'Conflicts' and 'Breaks' are not satisfied
				my $conflicts_koef = 1.0;
				foreach (@{$version->{conflicts}}, @{$version->{breaks}}) {
					# check if relation is accidentally satisfied
					my $ref_satisfying_versions = $self->{_cache}->get_satisfying_versions($_);

					if (!__is_version_array_intersects_with_packages($ref_satisfying_versions, $ref_current_packages)) {
						# good, nothing to do
					} else {
						# so, this can conflict... check it deeper on the fly
						my $conflict_found = 0;
						foreach my $satisfying_version (@$ref_satisfying_versions) {
							my $other_package_name = $satisfying_version->{package_name};

							# package can't conflict (or break) with itself
							$other_package_name ne $package_name or next;

							# is the package installed?
							exists $ref_current_packages->{$other_package_name} or next;

							my $other_package_entry = $ref_current_packages->{$other_package_name};

							# does the package have an installed version?
							defined($other_package_entry->[PE_VERSION]) or next;

							# is this our version?
							$other_package_entry->[PE_VERSION]->{version_string} eq $satisfying_version->{version_string} or next;

							# :(
							$conflict_found = 1;

							# additionally, in case of absense of stick, also contribute to possible actions
							if (!$other_package_entry->[PE_STICK]) {
								# so change it
								my $other_package = $self->{_cache}->get_binary_package($other_package_name);
								foreach my $other_version (@{$other_package->versions()}) {
									# don't try existing version
									next if $other_version->{version_string} eq $satisfying_version->{version_string};

									push @possible_actions, {
										'package_name' => $other_package_name,
										'version' => $other_version,
										'koef' => $conflicts_koef,
									};
								}

								if ($self->_is_package_can_be_removed($other_package_name)) {
									# or remove it
									push @possible_actions, {
										'package_name' => $other_package_name,
										'version' => undef,
										'koef' => $conflicts_koef,
									};
								}
							}
						}

						if ($conflict_found) {
							$check_failed = 1;

							# mark package as failed one more time
							++$failed_counts{$package_name};

							if (!$package_entry->[PE_STICK]) {
								# change version of the package
								my $package = $self->{_cache}->get_binary_package($package_name);
								foreach my $other_version (@{$package->versions()}) {
									# don't try existing version
									next if $other_version->{version_string} eq $version->{version_string};

									push @possible_actions, {
										'package_name' => $package_name,
										'version' => $other_version,
										'koef' => $conflicts_koef,
									};
								}
								
								if ($self->_is_package_can_be_removed($package_name)) {
									# remove the package
									push @possible_actions, {
										'package_name' => $package_name,
										'version' => undef,
										'koef' => $conflicts_koef,
									};
								}
							}

							# in any case, stick this package
							$package_entry->[PE_STICK] = 1;

							if ($self->{_config}->var('debug::resolver')) {
								my $stringified_relation = stringify_relation_expression($_);
								$sub_mydebug_wrapper->("problem: package '$package_name': " . 
										"satisfied conflicts/breaks '$stringified_relation'");
							}
							$recheck_needed = 0;
							last MAIN_LOOP;
						}
					}
				}
			}
		}

		if (!$check_failed) {
			# in case we go next
			$check_failed = 1;

			# if the solution was only just finished
			if ($self->{_config}->var('debug::resolver') && $ref_current_solution_entry->{finished}) {
				$sub_mydebug_wrapper->("finished");
			}
			$ref_current_solution_entry->{finished} = 1;
			# resolver can refuse the solution
			my $new_selected_solution_entry_index = $sub_solution_chooser->(\@solution_entries);

			if ($new_selected_solution_entry_index != $selected_solution_entry_index) {
				# ok, process other solution
				next;
			}

			# clean up automatically installed by resolver and now unneeded packages
			$self->_clean_automatically_installed($ref_current_packages);

			# build "user-frienly" version of solution
			my %suggested_packages;
			foreach my $package_name (keys %$ref_current_packages) {
				my $ref_package_entry = $ref_current_packages->{$package_name};
				$suggested_packages{$package_name}->{'version'} = $ref_package_entry->[PE_VERSION];

				$suggested_packages{$package_name}->{'manually_selected'} =
						$self->{_packages}->{$package_name}->[SPE_MANUALLY_SELECTED];
			}

			# suggest found solution
			if ($self->{_config}->var('debug::resolver')) {
				$sub_mydebug_wrapper->("proposing this solution");
			}
			my $user_answer = $sub_accept->(\%suggested_packages);
			if (!defined $user_answer) {
				# user has selected abandoning all further efforts
				goto EXIT;
			} elsif ($user_answer) {
				# yeah, this is end of our tortures
				if ($self->{_config}->var('debug::resolver')) {
					$sub_mydebug_wrapper->("accepted");
				}
				$return_code = 1;
				goto EXIT;
			} else {
				# caller hasn't accepted this solution, well, go next...
				if ($self->{_config}->var('debug::resolver')) {
					$sub_mydebug_wrapper->("declined");
				}
				# purge current solution
				splice @solution_entries, $selected_solution_entry_index, 1;
				next;
			}
		}

		if (scalar @possible_actions) {
			# firstly rank all solutions
			foreach (@possible_actions) {
				my $package_name = $_->{package_name};
				my $supposed_version = $_->{version};
				my $original_version = exists $ref_current_packages->{$package_name} ?
						$ref_current_packages->{$package_name}->[PE_VERSION] : undef;

				$_->{profit} //= $self->_get_action_profit($original_version, $supposed_version);
			}

			# sort them by "rank", from more bad to more good
			do {
				use sort 'stable';
				# don't try to remove 'reverse' and swap $a <-> $b
				#
				# using stable sort and reversing guarantees that possible
				# actions with equal profits will be processed in generated
				# order
				@possible_actions = reverse sort { $b->{profit} <=> $a->{profit} } @possible_actions;
			};

			my @forked_solution_entries;
			# fork the solution entry and apply all the solutions by one
			foreach my $idx (0..$#possible_actions) {
				my $ref_cloned_solution_entry;
				if ($idx == $#possible_actions) {
					# use existing solution entry
					$ref_cloned_solution_entry = $ref_current_solution_entry;
				} else {
					# clone the current stack to form a new one
					# we can obviously use Storable::dclone, or Clone::Clone here, but speed...
					$ref_cloned_solution_entry = {
						packages => __clone_packages($ref_current_solution_entry->{packages}),
						level => $ref_current_solution_entry->{level},
						score => $ref_current_solution_entry->{score},
						finished => 0,
					};
					push @forked_solution_entries, $ref_cloned_solution_entry;
				}

				# apply the solution
				my $ref_action_to_apply = $possible_actions[$idx];
				my $new_solution_identifier = $next_free_solution_identifier++;
				$sub_apply_action->($ref_cloned_solution_entry, $ref_action_to_apply, $new_solution_identifier);
				$ref_cloned_solution_entry->{identifier} = $new_solution_identifier;
			}

			# adding forked solutions to main solution storage just after current solution
			splice @solution_entries, $selected_solution_entry_index+1, 0, reverse @forked_solution_entries;

			# don't allow solution tree to grow unstoppably
			while (scalar @solution_entries > $self->{_params}->{'max-solution-count'}) {
				# find the worst solution and drop it
				my $min_score = $solution_entries[0]->{score};
				my $idx_of_min = 0;
				foreach my $idx (1..$#solution_entries) {
					if ($min_score > $solution_entries[$idx]->{score}) {
						$min_score = $solution_entries[$idx]->{score};
						$idx_of_min = $idx;
					}
				}
				$selected_solution_entry_index = $idx_of_min;
				if ($self->{_config}->var('debug::resolver')) {
					$sub_mydebug_wrapper->("dropping this solution");
				}
				splice @solution_entries, $idx_of_min, 1;
			}
		} else {
			if ($self->{_config}->var('debug::resolver')) {
				$sub_mydebug_wrapper->("no solution for broken package $package_name");
			}
			# purge current solution
			splice @solution_entries, $selected_solution_entry_index, 1;
		}
	}} while $check_failed;

	EXIT:
	delete $self->{_packages}->{$_dummy_package_name};
	return $return_code;
}

=head2 resolve

method, finds a solution for requested actions

Parameters:

I<sub_accept> - reference to subroutine which has to return true if solution is
accepted, false if solution is rejected, undef if user abandoned further searches

=cut

sub resolve ($$) {
	my ($self, $sub_accept) = @_;

	# unwinding relations
	while (scalar @{$self->{_pending_relations}}) {
		my $relation_expression = shift @{$self->{_pending_relations}};
		my $ref_satisfying_versions = $self->{_cache}->get_satisfying_versions($relation_expression);
		
		# if we have no candidates, skip the relation
		scalar @$ref_satisfying_versions or next;

		# installing most preferrable version

		my $version_to_install = $ref_satisfying_versions->[0];
		if ($self->{_config}->var('debug::resolver')) {
			mydebug("selected package '%s', version '%s' for relation expression '%s'",
					$version_to_install->{package_name},
					$version_to_install->{version_string},
					stringify_relation_expression($relation_expression)
			);
		}
		$self->_install_version_no_stick($version_to_install);
		# note that _install_version_no_stick can add some pending relations
	}

	# at this stage we have all extraneous dependencies installed, now we should check inter-depends
	return $self->_resolve($sub_accept);
}

1;

