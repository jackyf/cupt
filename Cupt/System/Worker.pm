package Cupt::System::Worker;

use warnings;
use strict;

use Cupt::Core;

=head1 FIELDS

I<config> - reference to Cupt::Config

I<desired_state> - { I<package_name> => { 'version' => I<version> } }

I<system_state> - reference to Cupt::System::State

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
	$ref_actions_preview = $self->get_actions_preview();
	# firstly, divide all actions into basic ones
	if (!defined $self->{desired_state}) {
		myinternaldie("worker desired state is not given");
	}
	foreach my $package_name (keys %{$self->{desired_state}}) {
		my $action;
		my $supposed_version = $self->{desired_state}->{$package_name}->{version};

	# TODO: extract loops and place them into single action groups

	# action = {
	# 	'package_name' => package
	# 	'version_string' => version_string,
	# 	'action_name' => ('deconfigure', 'configure', 'install', 'remove', 'purge')
	# }
	my %graph = ( 'actions' => [], 'edges' => {} );

}

1;

