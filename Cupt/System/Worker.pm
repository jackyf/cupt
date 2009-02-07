package Cupt::System::Worker;

use warnings;
use strict;

use Cupt::Core;

=head1 FIELDS

I<config> - reference to Cupt::Config

I<cache> - reference to Cupt::Cache

I<desired_state> - { I<package_name> => { 'version' => I<version> } }

I<system_state> - { I<package_name> => { 'version' => I<version> } }

=cut

use fields qw(config system_state desired_state);

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

=head2 do_actions

member function, performes planned actions

Returns true if successful, false otherwise

=cut

sub do_actions {
	my ($self) = @_;
	my $ref_actions_preview = $self->get_actions_preview();
	if (!defined $self->{desired_state}) {
		myinternaldie("worker desired state is not given");
	}

	# action = {
	# 	'package_name' => package
	# 	'version_string' => version_string,
	# 	'action_name' => ('unpack' | 'configure' | 'remove' | 'purge')
	# }
	my %graph = ( 'actions' => [], 'edges' => {} );

	# user action - action name from actions preview
	my %user_action_to_inner_actions = (
		'install' => [ 'unpack', 'configure' ],
		'upgrade' => [ 'unpack', 'configure' ],
		'downgrade' => [ 'unpack', 'configure' ],
		'configure' => [ 'configure' ],
		'deconfigure' => [ 'remove' ],
		'remove' => [ 'remove' ],
		'purge' => [ 'purge' ],
	);
	my %user_action_to_source_state = (
		'install' => $self->{desired_state},
		'upgrade' => $self->{desired_state},
		'downgrade' => $self->{desired_state},
		'configure' => $self->{system_state},
		'deconfigure' => $self->{system_state},
		'remove' => $self->{system_state},
		'purge' => $self->{system_state},
	);

	# convert all actions into inner ones
	foreach my $user_action (keys %$ref_actions_preview) {
		my $ref_actions_to_be_performed = $user_action_to_inner_actions{$user_action};
		my $source_state = $user_action_to_source_state{$user_action};

		foreach my $inner_action (@$ref_actions_to_be_performed) {
			foreach my $package_name (@{$ref_actions_preview->{$user_action}}) {
				my $version_string = $self->{desired_state}->{$package_name}->{version}->{version};
				push @{$graph{'actions'}}, {
						'package_name' => $package_name,
						'version_string' => $version_string,
						'action_name' => $inner_action,
				};
			}
		}
	}

	# initialize dependency lists
	push @{$graph{'edges'}}, [] for 0..@{$graph{'actions'}};

	# fill the actions' dependencies
	# legend: if $edge[$a] contains $b, then $action[$a] needs to be done before $action[$b]
	# this is the format used by tsort subroutine
	foreach my $inner_action_idx (0..@{$graph{'actions'}}) {
		my $ref_inner_action = $graph{'actions'}->[$inner_action_idx];

		if ($ref_inner_action->{'action_name'} eq 'unpack') {
			# if the package has pre-depends, they needs to be satisfied before
			# unpack (policy 7.2)
			my $desired_version = $self->{desired_state}->{$ref_inner_action->{'package_name'}}->{version};
			if (defined $desired_version->{pre_depends}) {
				foreach my $relation_expression (@$desired_version->{pre_depends}) {
					my $ref_satisfying_versions = $self->{cache}->get_satisfying_versions($relation_expression);

					my $solution_is_found = 0;
					# maybe, we have some needed version already installed?
					foreach my $other_version (@$ref_satisfying_versions) {
						if ($other_version->is_local()) {
							my $other_package_name = $other_version->{package_name};
							my $other_desired_version_string = $self->{desired_state}->{$other_package_name}->{version}->{version};
							#TODO: rename 'is_local' -> 'is_installed'
							if ($other_desired_version_string eq $other_version->{version}) {
								# package version that satisfies this pre-depends, already installed in system
								# and won't be removed
								$solution_is_found = 1;
								last;
							}
						}
					}

					next if $solution_is_found;

					# ok, then look for packages in desired system state
					SATISFYING_VERSIONS:
					foreach my $other_version (@$ref_satisfying_versions) {
						my $other_package_name = $other_version->{package_name};
						if (exists $self->{desired_state}->{$other_package_name}) {
							# yes, this package is to be installed
							my $other_desired_version_string = $self->{desired_state}->{$other_package_name}->{version}->{version};
							if ($other_desired_version_string eq $other_version->{version}) {
								# ok, we found the valid candidate for this relation
								if (grep { $_ eq $other_package_name } $ref_actions_preview->{'configure'}) {
									# the valid candidate is already unpacked (only needs configuring)
									# this satisfies the pre-dependency
									$solution_is_found = 1;
									last;
								} else {
									# unpack for candidate is to be satisfied
									my %candidate_action = (
										'package_name' => $other_package_name,
										'version_string' => $other_desired_version_string,
										'action_name' => 'unpack'
									);
									# search for the appropriate unpack action
									foreach my $idx (0..@{$graph{'actions'}}) {
										if (%candidate_action == %{$graph{'actions'}->[$idx]}) {
											# it's it!
											push @{$graph{'edges'}->[$idx]}, $inner_action_idx;

											$solution_is_found = 1;
											last SATISFYING_VERSIONS;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	# TODO: extract loops and place them into single action groups
}

1;

