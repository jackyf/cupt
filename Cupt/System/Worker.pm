package Cupt::System::Worker;

use 5.10.0;
use warnings;
use strict;

use Cupt::Core;

=head1 FIELDS

I<config> - reference to Cupt::Config

I<cache> - reference to Cupt::Cache

I<desired_state> - { I<package_name> => { 'version' => I<version> } }

I<system_state> - reference to Cupt::System::State;

=cut

use fields qw(config cache system_state desired_state);

=head1 METHODS

=head2 new

creates the worker

Parameters:

I<config>, I<system_state>. See appropriate fields in FIELDS
section.

=cut

sub new {
	my $class = shift;
	my $self = fields::new($class);
	$self->{config} = shift;
	$self->{cache} = shift;
	$self->{system_state} = shift;
	$self->{desired_state} = undef;
	return $self;
}

=head2 set_desired_state

member function, sets desired state of the system

Parameters:

I<desired_state> - see FIELDS section for its structure

=cut

sub set_desired_state ($$) {
	my ($self, $ref_desired_state) = @_;
	$self->{desired_state} = $ref_desired_state;
}

=head2 get_actions_preview

member function, returns actions to be done to achieve desired state of the system (I<desired_state>)

Returns:

  {
    'install' => I<packages>,
    'remove' => I<packages>,
    'purge' => I<packages>,
    'upgrade' => I<packages>,
    'downgrade' => I<packages>,
    'configure' => I<packages>,
    'deconfigure' => I<packages>,
  }

where I<packages> = [ I<package_name>... ]

=cut

sub get_actions_preview ($) {
	my ($self) = @_;
	my %result = (
		'install' => [],
		'remove' => [],
		'purge' => [],
		'upgrade' => [],
		'downgrade' => [],
		'configure' => [],
		'deconfigure' => [],
	);

	if (!defined $self->{desired_state}) {
		myinternaldie("worker desired state is not given");
	}
	foreach my $package_name (keys %{$self->{desired_state}}) {
		my $action;
		my $supposed_version = $self->{desired_state}->{$package_name}->{version};
		if (defined $supposed_version) {
			# some package version is to be installed
			if (!exists $self->{system_state}->{installed_info}->{$package_name}) {
				# no installed info for package
				$action = 'install';
			} else {
				# there is some installed info about package
				my $ref_installed_info = $self->{system_state}->{installed_info}->{$package_name};
				if ($ref_installed_info->{'status'} eq 'config-files') {
					# treat as the same as uninstalled
					$action = 'install';
				} elsif ($ref_installed_info->{'status'} eq 'unpacked' ||
					$ref_installed_info->{'status'} eq 'half-configured' ||
					$ref_installed_info->{'status'} eq 'half-installed')
				{
					if ($ref_installed_info->{'version'} eq $supposed_version->{version}) {
						# the same version, but the package was in some interim state
						$action = 'configure';
					} else {
						# some interim state, but other version
						$action = 'install';
					}
				} else {
					# otherwise some package version is installed
					my $version_comparison_result = Cupt::Core::compare_version_strings(
							$supposed_version->{version}, $ref_installed_info->{'version'});

					if ($version_comparison_result > 0) {
						$action = 'upgrade';
					} elsif ($version_comparison_result < 0) {
						$action = 'downgrade';
					}
				}
			}
		} else { 
			# package is to be removed
			if (exists $self->{system_state}->{installed_info}->{$package_name}) {
				# there is some installed info about package
				my $ref_installed_info = $self->{system_state}->{installed_info}->{$package_name};
				if ($ref_installed_info->{'status'} eq 'unpacked' ||
					$ref_installed_info->{'status'} eq 'half-configured' ||
					$ref_installed_info->{'status'} eq 'half-installed')
				{
					# package was in some interim state
					$action = 'deconfigure';
				} else {
					if ($self->{config}->var('cupt::worker::purge')) {
						# package is requested to be purged
						# do it only if we can
						if ($ref_installed_info->{'status'} eq 'config-files' ||
							$ref_installed_info->{'status'} eq 'installed')
						{
							$action = 'purge';
						}
					} else {
						# package is requested to be removed
						if ($ref_installed_info->{'status'} eq 'installed') {
							$action = 'remove';
						}
					}
				}
			}
		}
		defined $action and push @{$result{$action}}, $package_name;
	}
	return \%result;
}

# Auxiliary topological sort function
# downloaded from http://www.perlmonks.org/?node_id=84192
#
# Pass it as input a list of array references; these
# specify that that index into the list must come before all
# elements of its array. Output is a topologically sorted
# list of indices, or undef if input contains a cycle. Note
# that you must pass an array ref for every input
# elements (if necessary, by adding an empty list
# reference)
#
# For instance, tsort ([1,2,3], [3], [3], []) returns
# (0,2,1,3).

sub tsort {
	my @out = @_;
	my @ret;

	# Compute initial in degrees
	my @ind;
	for my $l (@out) {
		++$ind[$_] for (@$l)
	}

	# Work queue
	my @q;
	@q = grep { ! $ind[$_] } 0..$#out;

	# Loop
	while (@q) {
		my $el = pop @q;
		$ret[@ret] = $el;
		for (@{$out[$el]}) {
			push @q, $_ if (! --$ind[$_]);
		}
	}

	@ret == @out ? @ret : undef;
}

sub __is_inner_actions_equal ($$) {
	my ($ref_left_action, $ref_right_action) = @_;
	return ($ref_left_action->{'package_name'} eq $ref_right_action->{'package_name'} &&
			$ref_left_action->{'version_string'} eq $ref_right_action->{'version_string'} &&
			$ref_left_action->{'action_name'} eq $ref_right_action->{'action_name'});
}

sub _fill_actions ($$\@) {
	my ($self, $ref_actions_preview, $ref_result) = @_;

	# user action - action name from actions preview
	my %user_action_to_inner_actions = (
		'install' => [ 'unpack', 'configure' ],
		'upgrade' => [ 'unpack', 'configure' ],
		'downgrade' => [ 'unpack', 'configure' ],
		'configure' => [ 'configure' ],
		'deconfigure' => [ 'remove' ],
		'remove' => [ 'remove' ],
		'purge' => [ 'remove' ],
	);

	# convert all actions into inner ones
	foreach my $user_action (keys %$ref_actions_preview) {
		my $ref_actions_to_be_performed = $user_action_to_inner_actions{$user_action};

		foreach my $inner_action (@$ref_actions_to_be_performed) {
			foreach my $package_name (@{$ref_actions_preview->{$user_action}}) {
				my $version_string;
				if ($user_action eq 'install' ||
					$user_action eq 'upgrade' ||
					$user_action eq 'downgrade')
				{
					$version_string = $self->{desired_state}->{$package_name}->{version}->{version};
				} else {
					$version_string = $self->{system_state}->get_installed_version_string($package_name);
				}
				push @$ref_result, {
						'package_name' => $package_name,
						'version_string' => $version_string,
						'action_name' => $inner_action,
				};
			}
		}
	}
}

# fills ref_graph with dependencies specified in ref_relations_expressions
sub _fill_action_dependencies ($$$\%) {
	my ($self, $ref_relation_expressions, $action_name, $inner_action_idx, $ref_graph) = @_;

	if (defined $ref_relation_expressions) {
		foreach my $relation_expression (@$ref_relation_expressions) {
			my $ref_satisfying_versions = $self->{cache}->get_satisfying_versions($relation_expression);

			SATISFYING_VERSIONS:
			foreach my $other_version (@$ref_satisfying_versions) {
				my $other_package_name = $other_version->{package_name};
				my $other_version_string = $other_version->{version};
				my %candidate_action = (
					'package_name' => $other_package_name,
					'version_string' => $other_version_string,
					'action_name' => $action_name
				);
				# search for the appropriate action in action list
				foreach my $idx (0..$#{$ref_graph->{'actions'}}) {
					if (__is_inner_actions_equal(\%candidate_action, \%{$ref_graph->{'actions'}->[$idx]})) {
						# it's it!
						my $master_action_idx = $action_name eq 'remove' ? $idx : $inner_action_idx;
						my $slave_action_idx = $action_name eq 'remove' ? $inner_action_idx : $idx;

						push @{$ref_graph->{'edges'}->[$slave_action_idx]}, $master_action_idx;

						last SATISFYING_VERSIONS;
					}
				}
			}
		}
	}
}

=head2 do_actions

member function, performes planned actions

Returns true if successful, false otherwise

=cut

sub do_actions ($) {
	my ($self) = @_;
	my $ref_actions_preview = $self->get_actions_preview();
	if (!defined $self->{desired_state}) {
		myinternaldie("worker desired state is not given");
	}

	# action = {
	# 	'package_name' => package
	# 	'version_string' => version_string,
	# 	'action_name' => ('unpack' | 'configure' | 'remove')
	# }
	my %graph = ( 'actions' => [], 'edges' => [] );

	$self->_fill_actions($ref_actions_preview, \@{$graph{'actions'}});

	# initialize dependency lists
	push @{$graph{'edges'}}, [] for 1..@{$graph{'actions'}};

	# fill the actions' dependencies
	# legend: if $edge[$a] contains $b, then $action[$a] needs to be done before $action[$b]
	# this is the format used by tsort subroutine
	foreach my $inner_action_idx (0..@{$graph{'actions'}}) {
		my $ref_inner_action = $graph{'actions'}->[$inner_action_idx];

		my $package_name = $ref_inner_action->{'package_name'};
		given ($ref_inner_action->{'action_name'}) {
			when ('unpack') {
				# if the package has pre-depends, they needs to be satisfied before
				# unpack (policy 7.2)
				my $desired_version = $self->{desired_state}->{$package_name}->{version};
				# pre-depends must be unpacked before
				$self->_fill_action_dependencies(
						$desired_version->{pre_depends}, 'unpack', $inner_action_idx, \%graph);
			}
			when ('configure') {
				# configure can be done only after the unpack of the same version
				my $desired_version = $self->{desired_state}->{$package_name}->{version};

				# pre-depends must be configured before
				$self->_fill_action_dependencies(
						$desired_version->{pre_depends}, 'configure', $inner_action_idx, \%graph);
				# depends must be configured before
				$self->_fill_action_dependencies(
						$desired_version->{depends}, 'configure', $inner_action_idx, \%graph);

				# it has also to be unpacked if the same version was not in state 'unpacked'
				# search for the appropriate unpack action
				my %candidate_action = %$ref_inner_action;
				$candidate_action{'action_name'} = 'unpack';
				foreach my $idx (0..$#{$graph{'actions'}}) {
					if (__is_inner_actions_equal(\%candidate_action, \%{$graph{'actions'}->[$idx]})) {
						# found...
						push @{$graph{'edges'}->[$idx]}, $inner_action_idx;
						last;
					}
				}
			}
			when ('remove') {
				# package dependencies can be removed only after removal of the package
				my $package = $self->{cache}->get_binary_package($package_name);
				my $version_string = $ref_inner_action->{'version_string'};
				my $system_version = $self->{cache}->get_specific_version($package, $version_string);
				# pre-depends must be removed after
				$self->_fill_action_dependencies(
						$system_version->{pre_depends}, 'remove', $inner_action_idx, \%graph);
				# depends must be removed after
				$self->_fill_action_dependencies(
						$system_version->{depends}, 'remove', $inner_action_idx, \%graph);
			}
		}
	}

	# TODO: extract loops and place them into single action groups

	# topologic sort of actions
	my @sorted_action_indexes = tsort(@{$graph{'edges'}});

	defined $sorted_action_indexes[0] or
			mydie(__("unable to schedule dpkg commands"));

	# simulating actions
	foreach my $ref_action (map { $graph{'actions'}->[$_] } @sorted_action_indexes) {
		my $action_name = $ref_action->{'action_name'};
		my $package_expression = $ref_action->{'package_name'};
		if ($action_name eq 'unpack') {
			$package_expression .= '<' . $ref_action->{'version_string'} . '>';
		} elsif ($action_name eq 'remove' && $self->{config}->var('cupt::worker::purge')) {
			$action_name = 'purge';
		}
		say "simulating: dpkg --$action_name $package_expression";
	}
}

1;

