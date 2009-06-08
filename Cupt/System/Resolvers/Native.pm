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

use Cupt::Core;
use Cupt::Cache::Relation qw(stringify_relation_expression);

use Graph;
use constant {
	PE_VERSION => 0,
	PE_STICK => 1,
	PE_FAKE_SATISFIED => 2,
	PE_REASONS => 3,
	SPE_MANUALLY_SELECTED => 4,
	SPE_INSTALLED => 5,
};

our $_dummy_package_name = "<satisfy>";

=begin internal

=head2 _pending_relations

array of relations which are to be satisfied by final resolver, used for
filling depends, recommends (optionally), suggests (optionally) of requested
packages, or for satisfying some requested relations

=end internal

=cut

use fields qw(_old_packages _packages _pending_relations
		_strict_relation_expressions);

sub new {
	my $class = shift;
	my $self = fields::new($class);
	$self->SUPER::new(@_);

	$self->{_pending_relations} = [];
	$self->{_strict_relation_expressions} = [];

	return $self;
}

sub __new_package_entry () {
	my $ref_package_entry = [];
	$ref_package_entry->[PE_VERSION] = undef;
	$ref_package_entry->[PE_STICK] = 0;
	$ref_package_entry->[PE_FAKE_SATISFIED] = [];
	$ref_package_entry->[PE_REASONS] = [];
	$ref_package_entry->[SPE_MANUALLY_SELECTED] = 0;
	$ref_package_entry->[SPE_INSTALLED] = 0;
	return $ref_package_entry;
}

sub import_installed_versions ($$) {
	my ($self, $ref_versions) = @_;

	foreach my $version (@$ref_versions) {
		# just moving versions to packages, don't try install or remove some dependencies
		# '_packages' will be modified, leave '_old_packages' as original system state
		my $package_name = $version->{package_name};
		$self->{_packages}->{$package_name} = __new_package_entry();
		$self->{_packages}->{$package_name}->[PE_VERSION] = $version;
		$self->{_packages}->{$package_name}->[SPE_INSTALLED] = 1;
		@{$self->{_old_packages}->{$package_name}} = @{$self->{_packages}->{$package_name}};
	}
}

sub _schedule_new_version_relations ($$) {
	my ($self, $version) = @_;

	# unconditionally adding pre-depends
	foreach (@{$version->{pre_depends}}) {
		$self->_auto_satisfy_relation($_, [ $version, 'pre-depends', $_ ]);
	}
	# unconditionally adding depends
	foreach (@{$version->{depends}}) {
		$self->_auto_satisfy_relation($_, [ $version, 'depends', $_ ]);
	}
	if ($self->config->var('apt::install-recommends')) {
		# ok, so adding recommends
		foreach (@{$version->{recommends}}) {
			$self->_auto_satisfy_relation($_, [ $version, 'recommends', $_ ]);
		}
	}
	if ($self->config->var('apt::install-suggests')) {
		# ok, so adding suggests
		foreach (@{$version->{suggests}}) {
			$self->_auto_satisfy_relation($_, [ $version, 'suggests', $_ ]);
		}
	}
}

# installs new version, shedules new dependencies, but not sticks it
sub _install_version_no_stick ($$$) {
	my ($self, $version, $reason) = @_;
	
	my $package_name = $version->{package_name};
	$self->{_packages}->{$package_name} //= __new_package_entry();
	if ($self->{_packages}->{$package_name}->[PE_STICK])
	{
		# package is restricted to be updated
		return;
	}

	if ((not $self->{_packages}->{$package_name}->[PE_VERSION]) ||
		($self->{_packages}->{$package_name}->[PE_VERSION] != $version))
	{
		$self->{_packages}->{$package_name}->[PE_VERSION] = $version;
		if ($self->config->var('cupt::resolver::track-reasons')) {
			push @{$self->{_packages}->{$package_name}->[PE_REASONS]}, $reason;
		}
		if ($self->config->var('debug::resolver')) {
			mydebug("install package '$package_name', version '$version->{version_string}'");
		}
		$self->_schedule_new_version_relations($version);
	}
}

sub install_version ($$) {
	my ($self, $version) = @_;
	$self->_install_version_no_stick($version, [ 'user' ]);
	$self->{_packages}->{$version->{package_name}}->[PE_STICK] = 1;
	$self->{_packages}->{$version->{package_name}}->[SPE_MANUALLY_SELECTED] = 1;
}

sub satisfy_relation_expression ($$) {
	my ($self, $relation_expression) = @_;

	# schedule checking strict relation expression, it will be checked later
	push @{$self->{_strict_relation_expressions}}, $relation_expression;
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
}

sub remove_package ($$) {
	my ($self, $package_name) = @_;
	$self->{_packages}->{$package_name} //= __new_package_entry();
	$self->{_packages}->{$package_name}->[PE_VERSION] = undef;
	$self->{_packages}->{$package_name}->[PE_STICK] = 1;
	$self->{_packages}->{$package_name}->[SPE_MANUALLY_SELECTED] = 1;
	if ($self->config->var('cupt::resolver::track-reasons')) {
		push @{$self->{_packages}->{$package_name}->[PE_REASONS]}, [ 'user' ];
	}
	if ($self->config->var('debug::resolver')) {
		mydebug("removing package $package_name");
	}
}

sub upgrade ($) {
	my ($self) = @_;
	foreach (keys %{$self->{_packages}}) {
		my $package_name = $_;
		my $package = $self->cache->get_binary_package($package_name);
		my $original_version = $self->{_packages}->{$package_name}->[PE_VERSION];
		my $supposed_version = $self->cache->get_policy_version($package);
		# no need to install the same version
		$original_version->{version_string} ne $supposed_version->{version_string} or next;
		$self->_install_version_no_stick($supposed_version, [ 'user' ]);
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

	return 0 if not defined $version;

	my $factor = 1.0;
	my $package_name = $version->{package_name};
	if ($version->is_installed() && $self->cache->is_automatically_installed($package_name)) {
		# automatically installed packages count nothing for user
		$factor /= 20.0;
	}
	$factor *= 5.0 if defined($version->{essential});

	# omitting priority 'standard' here
	if ($version->{priority} eq 'optional') {
		$factor *= 0.9;
	} elsif ($version->{priority} eq 'extra') {
		$factor *= 0.7;
	} elsif ($version->{priority} eq 'important') {
		$factor *= 1.4;
	} elsif ($version->{priority} eq 'required') {
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

	return 
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
	return !$self->config->var('cupt::resolver::no-remove')
			|| !$self->{_packages}->{$package_name}->[SPE_INSTALLED];
}

sub _get_dependencies_groups ($$) {
	my ($self, $version) = @_;

	# action factor will determine the valuability of action
	# usually it will be 1.0 for strong dependencies and < 1.0 for soft dependencies
	my @result;
	push @result, { 'name' => 'pre-depends', 'relation_expressions' => $version->{pre_depends}, 'factor' => 2.0 };
	push @result, { 'name' => 'depends', 'relation_expressions' => $version->{depends}, 'factor' => 1.0 };
	if ($self->config->var('cupt::resolver::keep-recommends')) {
		push @result, { 'name' => 'recommends', 'relation_expressions' => $version->{recommends}, 'factor' => 0.4 };
	}
	if ($self->config->var('cupt::resolver::keep-suggests')) {
		push @result, { 'name' => 'suggests', 'relation_expressions' => $version->{suggests}, 'factor' => 0.1 };
	}

	return \@result;
}

sub _get_antidependencies_groups ($$) {
	my ($self, $version) = @_;

	# action factor will determine the valuability of action
	my @result;
	push @result, { 'name' => 'conflicts', 'relation_expressions' => $version->{conflicts}, 'factor' => 1.0 };
	push @result, { 'name' => 'breaks', 'relation_expressions' => $version->{breaks}, 'factor' => 1.0 };
	return \@result;
}

sub __clone_packages ($) {
	my ($ref_packages) = @_;

	my %clone;
	foreach (keys %$ref_packages) {
		my $ref_new_package_entry = $ref_packages->{$_};
		$clone{$_}->[PE_VERSION] = $ref_new_package_entry->[PE_VERSION];
		$clone{$_}->[PE_STICK] = $ref_new_package_entry->[PE_STICK];
		$clone{$_}->[PE_FAKE_SATISFIED] = [ @{$ref_new_package_entry->[PE_FAKE_SATISFIED]} ];
		$clone{$_}->[PE_REASONS] = [ @{$ref_new_package_entry->[PE_REASONS]} ];
	}
	return \%clone;
}

sub _clean_automatically_installed ($) {
	my ($self, $ref_packages) = @_;

	# firstly, prepare all package names that can be potentially removed
	my $can_autoremove = $self->config->var('cupt::resolver::auto-remove');
	my %candidates_for_remove;
	foreach my $package_name (keys %$ref_packages) {
		my $ref_package_entry = $ref_packages->{$package_name};
		my $version = $ref_package_entry->[PE_VERSION];
		defined $version or next;
		!$self->{_packages}->{$package_name}->[SPE_MANUALLY_SELECTED] or next;

		my $can_autoremove_this_package = $can_autoremove ?
				$self->cache->is_automatically_installed($package_name) : 0;
		my $package_was_installed = exists $self->{_old_packages}->{$package_name};
		(!$package_was_installed or $can_autoremove_this_package) or next;

		grep { $package_name eq $_ } $self->config->var('apt::neverautoremove') and next;
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
			if ($self->config->var('cupt::resolver::keep-recommends')) {
				push @valuable_relation_expressions, @{$version->{recommends}};
			}
			if ($self->config->var('cupt::resolver::keep-suggests')) {
				push @valuable_relation_expressions, @{$version->{suggests}};
			}

			foreach (@valuable_relation_expressions) {
				my $satisfying_versions = $self->cache->get_satisfying_versions($_);
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
			if ($remove) {
				$ref_packages->{$package_name}->[PE_VERSION] = undef;
				# leave only one reason :)
				@{$ref_packages->{$package_name}->[PE_REASONS]} = ( [ 'auto-remove' ] );
			}
		}
	};

	# also remove dummy package
	delete $ref_packages->{$_dummy_package_name};
}

sub _select_solution_chooser ($) {
	my ($self) = @_;

	my %solution_choosers = (
		'first-good' => \&__first_good_chooser,
		'multiline-fair' => \&__multiline_fair_chooser,
		'multiline-full' => \&__multiline_full_chooser,
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
	my $version = {
		package_name => $_dummy_package_name,
		pre_depends => [],
		depends => [],
		recommends => [],
		suggests => [],
		breaks => [],
		conflicts => [],
	};
	$self->{_packages}->{$_dummy_package_name} = __new_package_entry();
	$self->{_packages}->{$_dummy_package_name}->[PE_VERSION] = $version;
	$self->{_packages}->{$_dummy_package_name}->[PE_STICK] = 1;
	$self->{_packages}->{$_dummy_package_name}->[PE_VERSION]->{depends} =
			$self->{_strict_relation_expressions};
}

sub _apply_action ($$$$$) {
	my ($self, $ref_solution_entry, $ref_action_to_apply, $new_solution_identifier, $sub_mydebug_wrapper) = @_;

	my $package_name_to_change = $ref_action_to_apply->{'package_name'};
	my $supposed_version = $ref_action_to_apply->{'version'};

	# stick all requested package names
	foreach my $package_name (@{$ref_action_to_apply->{'package_names_to_stick'}}) {
		$ref_solution_entry->{packages}->{$package_name} //= __new_package_entry();
		$ref_solution_entry->{packages}->{$package_name}->[PE_STICK] = 1;
	}

	my $ref_package_entry_to_change = $ref_solution_entry->{packages}->{$package_name_to_change};
	my $original_version = $ref_package_entry_to_change->[PE_VERSION];

	my $profit = $ref_action_to_apply->{'profit'} //
			$self->_get_action_profit($original_version, $supposed_version);
	$profit *= $ref_action_to_apply->{'factor'};

	if ($self->config->var('debug::resolver')) {
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
	$ref_package_entry_to_change->[PE_VERSION] = $supposed_version;
	if (defined $ref_action_to_apply->{'fakely_satisfies'}) {
		push @{$ref_package_entry_to_change->[PE_FAKE_SATISFIED]}, $ref_action_to_apply->{'fakely_satisfies'};
	}
	if ($self->{_config}->var('cupt::resolver::track-reasons')) {
		if (defined $ref_action_to_apply->{'reason'}) {
			push @{$ref_package_entry_to_change->[PE_REASONS]}, $ref_action_to_apply->{'reason'};
		}
	}
}

sub _get_actions_to_fix_dependency ($$$$$$$) {
	my ($self, $ref_packages, $package_name, $ref_satisfying_versions,
			$relation_expression, $dependency_group_name, $dependency_group_factor) = @_;

	my @result;

	my $version = $ref_packages->{$package_name}->[PE_VERSION];

	# install one of versions package needs
	foreach my $satisfying_version (@$ref_satisfying_versions) {
		my $satisfying_package_name = $satisfying_version->{package_name};
		if (!exists $ref_packages->{$satisfying_package_name} ||
			!$ref_packages->{$satisfying_package_name}->[PE_STICK])
		{
			push @result, {
				'package_name' => $satisfying_package_name,
				'version' => $satisfying_version,
				'factor' => $dependency_group_factor,
				'reason' => [ $version, $dependency_group_name, $relation_expression ],
			};
		}
	}

	# change this package
	if (!$ref_packages->{$package_name}->[PE_STICK]) {
		# change version of the package
		my $other_package = $self->cache->get_binary_package($package_name);
		foreach my $other_version (@{$other_package->get_versions()}) {
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
					my $ref_candidate_satisfying_versions = $self->cache->get_satisfying_versions($_);
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
					push @result, {
						'package_name' => $package_name,
						'version' => $other_version,
						'factor' => $dependency_group_factor,
						'reason' => [ $version, $dependency_group_name, $relation_expression ],
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
				'reason' => [ $version, $dependency_group_name, $relation_expression ],
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
}

sub _resolve ($$) {
	my ($self, $sub_accept) = @_;

	my $sub_solution_chooser = $self->_select_solution_chooser();
	if ($self->config->var('debug::resolver')) {
		mydebug("started resolving");
	}
	$self->_require_strict_relation_expressions();

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
		$self->_apply_action($ref_solution_entry, $ref_action_to_apply, $new_solution_identifier, $sub_mydebug_wrapper);
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
			foreach my $package_name (@packages_in_order) {
				$package_entry = $ref_current_packages->{$package_name};
				my $version = $package_entry->[PE_VERSION];
				defined $version or next;

				# checking that all dependencies are satisfied
				foreach my $ref_dependency_group (@{$self->_get_dependencies_groups($version)}) {
					my $dependency_group_factor = $ref_dependency_group->{'factor'};
					my $dependency_group_name = $ref_dependency_group->{'name'};
					foreach my $relation_expression (@{$ref_dependency_group->{relation_expressions}}) {
						# check if relation is already satisfied
						my $ref_satisfying_versions = $self->cache->get_satisfying_versions($relation_expression);
						if (!__is_version_array_intersects_with_packages($ref_satisfying_versions, $ref_current_packages)) {
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
										'factor' => $dependency_group_factor,
										# set profit manually, as we are inserting fake action here
										'profit' => -50,
										'fakely_satisfies' => $relation_expression,
										'reason' => undef,
									};
								}
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
				foreach my $ref_dependency_group (@{$self->_get_antidependencies_groups($version)}) {
					my $dependency_group_factor = $ref_dependency_group->{'factor'};
					my $dependency_group_name = $ref_dependency_group->{'name'};
					foreach my $relation_expression (@{$ref_dependency_group->{relation_expressions}}) {
						# check if relation is accidentally satisfied
						my $ref_satisfying_versions = $self->cache->get_satisfying_versions($relation_expression);
						if (__is_version_array_intersects_with_packages($ref_satisfying_versions, $ref_current_packages)) {
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
									my $other_package = $self->cache->get_binary_package($other_package_name);
									foreach my $other_version (@{$other_package->get_versions()}) {
										# don't try existing version
										next if $other_version->{version_string} eq $satisfying_version->{version_string};

										push @possible_actions, {
											'package_name' => $other_package_name,
											'version' => $other_version,
											'factor' => $dependency_group_factor,
											'reason' => [ $version, $dependency_group_name, $relation_expression ],
										};
									}

									if ($self->_is_package_can_be_removed($other_package_name)) {
										# or remove it
										push @possible_actions, {
											'package_name' => $other_package_name,
											'version' => undef,
											'factor' => $dependency_group_factor,
											'reason' => [ $version, $dependency_group_name, $relation_expression ],
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
									my $package = $self->cache->get_binary_package($package_name);
									foreach my $other_version (@{$package->get_versions()}) {
										# don't try existing version
										next if $other_version->{version_string} eq $version->{version_string};

										push @possible_actions, {
											'package_name' => $package_name,
											'version' => $other_version,
											'factor' => $dependency_group_factor,
											'reason' => [ $version, $dependency_group_name, $relation_expression ],
										};
									}
									
									if ($self->_is_package_can_be_removed($package_name)) {
										# remove the package
										push @possible_actions, {
											'package_name' => $package_name,
											'version' => undef,
											'factor' => $dependency_group_factor,
											'reason' => [ $version, $dependency_group_name, $relation_expression ],
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

		if (!$check_failed) {
			# in case we go next
			$check_failed = 1;

			# if the solution was only just finished
			if ($self->config->var('debug::resolver') && $ref_current_solution_entry->{finished}) {
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
				$suggested_packages{$package_name}->{'reasons'} = $ref_package_entry->[PE_REASONS];
				$suggested_packages{$package_name}->{'manually_selected'} =
						$self->{_packages}->{$package_name}->[SPE_MANUALLY_SELECTED];
			}

			# suggest found solution
			if ($self->config->var('debug::resolver')) {
				$sub_mydebug_wrapper->("proposing this solution");
			}
			my $user_answer = $sub_accept->(\%suggested_packages);
			if (!defined $user_answer) {
				# user has selected abandoning all further efforts
				goto EXIT;
			} elsif ($user_answer) {
				# yeah, this is end of our tortures
				if ($self->config->var('debug::resolver')) {
					$sub_mydebug_wrapper->("accepted");
				}
				$return_code = 1;
				goto EXIT;
			} else {
				# caller hasn't accepted this solution, well, go next...
				if ($self->config->var('debug::resolver')) {
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
			while (scalar @solution_entries > $self->config->var('cupt::resolver::max-solution-count')) {
				# find the worst solution and drop it
				my $min_normalized_score = __normalized_score($solution_entries[0]);
				my $idx_of_min = 0;
				foreach my $idx (1..$#solution_entries) {
					my $current_normalized_score = __normalized_score($solution_entries[$idx]);
					if ($min_normalized_score > $current_normalized_score) {
						$min_normalized_score = $current_normalized_score;
						$idx_of_min = $idx;
					}
				}
				$selected_solution_entry_index = $idx_of_min;
				if ($self->config->var('debug::resolver')) {
					$sub_mydebug_wrapper->("dropping this solution");
				}
				splice @solution_entries, $idx_of_min, 1;
			}
		} else {
			if ($self->config->var('debug::resolver')) {
				$sub_mydebug_wrapper->("no solutions");
			}
			# purge current solution
			splice @solution_entries, $selected_solution_entry_index, 1;
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
					$version_to_install->{package_name},
					$version_to_install->{version_string},
					stringify_relation_expression($relation_expression)
			);
		}
		my $reason = $ref_pending_relation->{'reason'};
		$self->_install_version_no_stick($version_to_install, $reason);
		# note that _install_version_no_stick can add some pending relations
	}

	# at this stage we have all extraneous dependencies installed, now we should check inter-depends
	return $self->_resolve($sub_accept);
}

1;

