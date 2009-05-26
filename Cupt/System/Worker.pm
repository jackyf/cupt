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
package Cupt::System::Worker;

=head1 NAME

Cupt::System::Worker - system modifier for Cupt

=cut

use 5.10.0;
use warnings;
use strict;

use Graph;
use Digest;
use Fcntl qw(:seek :DEFAULT);
use List::Util qw(sum);
use File::Copy;
use File::Basename;
use POSIX;

use Cupt::Core;
use Cupt::Cache;
use Cupt::Download::Manager;

my $_download_partial_suffix = '/partial';

use fields qw(_config _cache _system_state _desired_state);

=head1 METHODS

=head2 new

creates new Cupt::System::Worker object

Parameters:

I<config> - reference to L<Cupt::Config|Cupt::Config>

I<cache> - reference to L<Cupt::Cache|Cupt::Cache>

=cut

sub new {
	my $class = shift;
	my $self = fields::new($class);
	$self->{_config} = shift;
	$self->{_cache} = shift;
	$self->{_system_state} = $self->{_cache}->get_system_state();
	$self->{_desired_state} = undef;
	$self->_syncronize_apt_compat_symlinks();
	return $self;
}

sub _syncronize_apt_compat_symlinks ($) {
	my ($self) = @_;

	return if $self->{_config}->var('cupt::worker::simulate');

	my $archives_location = $self->_get_archives_location();
	my @debs = glob("$archives_location/*.deb");
	foreach my $deb (@debs) {
		if (-l $deb) {
			# this is a symlink

			# fill info about pointed file
			stat $deb;
			if (not -e _) {
				# this is dangling symlink
				unlink $deb or
						mywarn("unable to delete dangling APT compatibility symbolic link '%s': %s", $deb, $!);
			}
		} elsif (-f $deb) {
			# this is a regular file
			my $basename = basename($deb);
			(my $corrected_basename = $basename) =~ s/%3a/:/;

			my $corrected_deb = "$archives_location/$corrected_basename";

			next if -e $corrected_deb;

			if ($corrected_basename ne $basename) {
				symlink $basename, $corrected_deb or
						mywarn("unable to create APT compatibility symbolic link '%s' -> '%s': %s",
								$corrected_deb, $basename, $!);
			}
		}
	}
}

=head2 set_desired_state

method, sets desired state of the system

Parameters:

I<desired_state> - the desired state after the actions, hash reference:

  { I<package_name> =>
    {
      'version' => I<version>,
      'manually_selected' => boolean
      'reasons' => [ I<reason>... ]
    }
  }

where:

I<version> - reference to L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion>

TODO: reason description

=cut

sub set_desired_state ($$) {
	my ($self, $ref_desired_state) = @_;
	$self->{_desired_state} = $ref_desired_state;
}

sub _get_archives_location ($) {
	my ($self) = @_;
	return $self->{_config}->var('dir') .
			$self->{_config}->var('dir::cache') . '/' .
			$self->{_config}->var('dir::cache::archives');
}

sub __get_archive_basename ($) {
	my ($version) = @_;

	return $version->{package_name} . '_' .
			$version->{version_string} . '_' .
			$version->{architecture} . '.deb';
}

sub __verify_hash_sums ($$) {
	my ($version, $path) = @_;

	my @checks = 	(
					[ $version->{md5sum}, 'MD5' ],
					[ $version->{sha1sum}, 'SHA-1' ],
					[ $version->{sha256sum}, 'SHA-256' ],
					);
	open(FILE, '<', $path) or
			mydie("unable to open file '%s': %s", $path, $!);
	binmode(FILE);

	foreach (@checks) {
		my $expected_result = $_->[0];
		my $hash_type = $_->[1];
		my $hasher = Digest->new($hash_type);
		seek(FILE, 0, SEEK_SET);
		$hasher->addfile(*FILE);
		my $computed_sum = $hasher->hexdigest();
		return 0 if ($computed_sum ne $expected_result);
	}

	close(FILE) or
			mydie("unable to close file '%s': %s", $path, $!);

	return 1;
}

=head2 get_actions_preview

method, returns actions to be done to achieve desired state of the system (I<desired_state>)

Returns:

  {
    'install' => I<packages>,
    'remove' => I<packages>,
    'purge' => I<packages>,
    'upgrade' => I<packages>,
    'downgrade' => I<packages>,
    'configure' => I<packages>,
    'deconfigure' => I<packages>,
    'markauto' => [ $package_name ... ]
    'unmarkauto' => [ $package_name ... ]
  }

where:

I<packages> = [
                {
                  'package_name' => $package_name,
                  'version' => I<version>,
				  'reasons' => [ I<reason>... ]
                }...
              ]

I<version> - reference to L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion>

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
		'markauto' => [],
		'unmarkauto' => [],
	);

	if (!defined $self->{_desired_state}) {
		myinternaldie("worker desired state is not given");
	}
	foreach my $package_name (keys %{$self->{_desired_state}}) {
		my $action;
		my $ref_supposed_package_entry = $self->{_desired_state}->{$package_name};
		my $supposed_version = $ref_supposed_package_entry->{'version'};
		my $ref_installed_info = $self->{_system_state}->get_installed_info($package_name);
		if (defined $supposed_version) {
			# some package version is to be installed
			if (!defined $ref_installed_info) {
				# no installed info for package
				$action = 'install';
			} else {
				# there is some installed info about package
				if ($ref_installed_info->{'status'} eq 'config-files') {
					# treat as the same as uninstalled
					$action = 'install';
				} elsif ($ref_installed_info->{'status'} eq 'unpacked' ||
					$ref_installed_info->{'status'} eq 'half-configured' ||
					$ref_installed_info->{'status'} eq 'half-installed')
				{
					if ($ref_installed_info->{'version_string'} eq $supposed_version->{version_string}) {
						# the same version, but the package was in some interim state
						$action = 'configure';
					} else {
						# some interim state, but other version
						$action = 'install';
					}
				} else {
					# otherwise some package version is installed
					my $version_comparison_result = Cupt::Core::compare_version_strings(
							$supposed_version->{version_string}, $ref_installed_info->{'version_string'});

					if ($version_comparison_result > 0) {
						$action = 'upgrade';
					} elsif ($version_comparison_result < 0) {
						$action = 'downgrade';
					}
				}
			}
		} else { 
			# package is to be removed
			if (defined $ref_installed_info) {
				# there is some installed info about package
				if ($ref_installed_info->{'status'} eq 'unpacked' ||
					$ref_installed_info->{'status'} eq 'half-configured' ||
					$ref_installed_info->{'status'} eq 'half-installed')
				{
					# package was in some interim state
					$action = 'deconfigure';
				} else {
					if ($self->{_config}->var('cupt::worker::purge')) {
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
		if (defined $action) {
			my $ref_entry;
			$ref_entry->{'package_name'} = $package_name;
			$ref_entry->{'version'} = $supposed_version;
			$ref_entry->{'reasons'} = $ref_supposed_package_entry->{'reasons'};
			push @{$result{$action}}, $ref_entry;

			if ($action eq 'remove' || ($action eq 'purge' && $ref_installed_info->{'status'} eq 'installed')) {
				# in case of removing a package we delete the 'automatically
				# installed' info regardless was this flag set or not so next
				# time when this package is installed it has 'clean' info
				push @{$result{'unmarkauto'}}, $package_name;
			} elsif ($action eq 'install' && !$ref_supposed_package_entry->{'manually_selected'}) {;
				# set 'automatically installed' for new non-manually selected packages
				push @{$result{'markauto'}}, $package_name;
			}
		}
	}

	return \%result;
}

=head2 get_download_sizes_preview

Parameters:

I<ref_actions_preview> - supply result of L</get_actions_preview> here

Returns (I<total_bytes>, I<need_bytes>);

I<total_bytes> - total byte count needed for action,
I<need_bytes> - byte count, needed to download, <= I<total_bytes>

=cut

sub get_download_sizes_preview ($$) {
	my ($self, $ref_actions_preview) = @_;

	my $archives_location = $self->_get_archives_location();
	my $total_bytes = 0;
	my $need_bytes = 0;
	my @ref_package_entries = map { @{$ref_actions_preview->{$_}} } ('install', 'upgrade', 'downgrade');
	foreach my $ref_package_entry (@ref_package_entries) {
		my $version = $ref_package_entry->{'version'};
		my $size = $version->{size};
		$total_bytes += $size;
		$need_bytes += $size; # for start
		my $basename = __get_archive_basename($version);
		my $path = $archives_location . '/' . $basename;
		-e $path or next; # skip if the file is not present in the cache dir
		__verify_hash_sums($version, $path) or next; # skip if the file is not what we want
		# ok, no need to download the file
		$need_bytes -= $size;
	}

	return ($total_bytes, $need_bytes);
}

=head2 get_unpacked_sizes_preview

returns changes in unpacked sizes after whole operation for each package

Parameters:

I<ref_actions_preview> - supply result of L</get_actions_preview> here

Returns: { $package_name => $size_change }

=cut

sub get_unpacked_sizes_preview ($$) {
	my ($self, $ref_actions_preview) = @_;

	my %result;

	# install
	foreach my $ref_package_entry (@{$ref_actions_preview->{'install'}}) {
		my $version = $ref_package_entry->{'version'};
		$result{$ref_package_entry->{'package_name'}} = $version->{installed_size};
	}

	# remove/purge
	foreach my $ref_package_entry (@{$ref_actions_preview->{'remove'}}, @{$ref_actions_preview->{'purge'}}) {
		my $package_name = $ref_package_entry->{'package_name'};
		my $old_version = $self->{_cache}->get_binary_package($package_name)->get_installed_version();
		$result{$ref_package_entry->{'package_name'}} = - $old_version->{installed_size};
	}

	# upgrade/downgrade
	foreach my $ref_package_entry (@{$ref_actions_preview->{'upgrade'}}, @{$ref_actions_preview->{'downgrade'}}) {
		my $new_version = $ref_package_entry->{'version'};
		my $package_name = $ref_package_entry->{'package_name'};
		my $old_version = $self->{_cache}->get_binary_package($package_name)->get_installed_version();
		$result{$package_name} = $new_version->{installed_size} - $old_version->{installed_size};
	}

	# deconfigure
	foreach my $ref_package_entry (@{$ref_actions_preview->{'deconfigure'}}) {
		my $package_name = $ref_package_entry->{'package_name'};
		if ($self->{_config}->var('dir::state::status') !~ m{/status$}) {
			mywarn("unable to determine installed size for package '%s'", $package_name);
			$result{$package_name} = 0;
		} else {
			(my $admindir = $self->{_config}->var('dir::state::status')) =~ s{/status$}{};
			$result{$package_name} = - qx/dpkg-query --admindir=$admindir -f '\${Installed-Size}' --show $package_name/;
		}
	}

	# configure is uninteresting, it doesn't change unpacked size in system
	foreach my $ref_package_entry (@{$ref_actions_preview->{'configure'}}) {
		$result{$ref_package_entry->{'package_name'}} = 0;
	}

	# installed sizes are specified in kibibytes, convert them to bytes
	map { $_ *= 1024 } values %result;

	return \%result;
}

sub __is_inner_actions_equal ($$) {
	my ($ref_left_action, $ref_right_action) = @_;
	my $left_version = $ref_left_action->{'version'};
	my $right_version = $ref_right_action->{'version'};
	return ($left_version->{package_name} eq $right_version->{package_name} &&
			$left_version->{version_string} eq $right_version->{version_string} &&
			$ref_left_action->{'action_name'} eq $ref_right_action->{'action_name'});
}

sub _fill_actions ($$\@) {
	my ($self, $ref_actions_preview, $graph) = @_;

	# user action - action name from actions preview
	my %user_action_to_inner_actions = (
		'install' => [ 'unpack', 'configure' ],
		'upgrade' => [ 'remove', 'unpack', 'configure' ],
		'downgrade' => [ 'remove', 'unpack', 'configure' ],
		'configure' => [ 'configure' ],
		'deconfigure' => [ 'remove' ],
		'remove' => [ 'remove' ],
		'purge' => [ 'remove' ],
	);

	# convert all actions into inner ones
	foreach my $user_action (keys %$ref_actions_preview) {
		my $ref_actions_to_be_performed = $user_action_to_inner_actions{$user_action};

		foreach my $inner_action (@$ref_actions_to_be_performed) {
			foreach my $ref_package_entry (@{$ref_actions_preview->{$user_action}}) {
				my $version;
				if ($inner_action eq 'remove') {
					my $package_name = $ref_package_entry->{package_name};
					$version = $self->{_cache}->get_binary_package($package_name)->get_installed_version();
				} else {
					$version = $ref_package_entry->{'version'};
				}
				$graph->add_vertex({
						'version' => $version,
						'action_name' => $inner_action,
				});
			}
		}
	}
}

sub __stringify_inner_action ($) {
	my ($ref_action) = @_;

	my $package_name = $ref_action->{'version'}->{package_name};
	my $version_string = $ref_action->{'version'}->{version_string};
	my $action_name = $ref_action->{'action_name'};
	return "$action_name $package_name $version_string";
}

# fills ref_graph with dependencies specified in ref_relations_expressions
sub _fill_action_dependencies ($$$$) {
	my ($self, $ref_relation_expressions, $action_name, $direction, $ref_inner_action, $graph) = @_;

	foreach my $relation_expression (@$ref_relation_expressions) {
		my $ref_satisfying_versions = $self->{_cache}->get_satisfying_versions($relation_expression);

		SATISFYING_VERSIONS:
		foreach my $other_version (@$ref_satisfying_versions) {
			my %candidate_action = (
				'version' => $other_version,
				'action_name' => $action_name
			);
			# search for the appropriate action in action list
			foreach my $ref_current_action ($graph->vertices()) {
				if (__is_inner_actions_equal(\%candidate_action, $ref_current_action)) {
					# it's it!
					my $ref_master_action = $direction eq 'after' ? $ref_current_action : $ref_inner_action;
					my $ref_slave_action = $direction eq 'after' ? $ref_inner_action : $ref_current_action;

					$graph->add_edge($ref_slave_action, $ref_master_action);

					# adding relation to attributes
					my $ref_attributes = $graph->get_edge_attributes($ref_slave_action, $ref_master_action);
					if (exists $ref_attributes->{'relation_expressions'}) {
						push @{$ref_attributes->{'relation_expressions'}}, $relation_expression;
						$graph->set_edge_attributes($ref_slave_action, $ref_master_action, $ref_attributes);
					} else {
						$graph->set_edge_attributes($ref_slave_action, $ref_master_action,
								{ 'relation_expressions' => [ $relation_expression ] });
					}

					if ($self->{_config}->var('debug::worker')) {
						my $slave_string = __stringify_inner_action($ref_slave_action);
						my $master_string = __stringify_inner_action($ref_master_action);
						mydebug("new action dependency: '$slave_string' -> '$master_string'");
					}

					last SATISFYING_VERSIONS;
				}
			}
		}
	}
}

=head2 mark_as_automatically_installed

method, marks group of packages as automatically/manually installed

Parameters:

I<value> - boolean value: true - mark as automatically installed, false - mark
as manually installed

I<package_name>... - array of of package names to mark

=cut

sub mark_as_automatically_installed ($$;@) {
	my ($self, $markauto, @package_names) = @_;
	my $simulate = $self->{_config}->var('cupt::worker::simulate');

	if ($simulate) {
		foreach my $package_name (@package_names) {
			my $prefix = $markauto ?
					__('marking as automatically installed') : __('marking as manually installed');
			say __("simulating") . ": $prefix: $package_name";
		}
	} else {
		my $ref_extended_info = $self->{_cache}->get_extended_info();

		my $ref_autoinstalled_packages = $ref_extended_info->{'automatically_installed'};
		foreach my $package_name (@package_names) {
			$ref_autoinstalled_packages->{$package_name} = $markauto;
		}

		my @refreshed_autoinstalled_packages = grep { $ref_autoinstalled_packages->{$_} }
				keys %$ref_autoinstalled_packages;

		my $extended_info_file = $self->{_cache}->_path_of_extended_states();
		my $temp_file = $extended_info_file . '.cupt.tmp';

		sysopen(TEMP, $temp_file, O_WRONLY | O_EXCL | O_CREAT) or
				mydie("unable to open temporary file '%s': %s", $temp_file, $!);

		# filling new info
		foreach my $package_name (@refreshed_autoinstalled_packages) {
			print TEMP "Package: $package_name\nAuto-Installed: 1\n\n" or
					mydie("unable to write to file '%s': %s", $temp_file, $!);
		}

		close(TEMP) or
				mydie("unable to close temporary file '%s': %s", $temp_file, $!);
		move($temp_file, $extended_info_file) or
				mydie("unable to rename temporary file '%s' to extended states file '%s': %s",
						$temp_file, $extended_info_file, $!);
	}
}

sub _build_actions_graph ($$) {
	my ($self, $ref_actions_preview) = @_;

	if (!defined $self->{_desired_state}) {
		myinternaldie("worker desired state is not given");
	}

	# action = {
	# 	'package_name' => package
	# 	'version_string' => version_string,
	# 	'action_name' => ('unpack' | 'configure' | 'remove')
	# }
	my $graph = new Graph('directed' => 1, 'refvertexed' => 1);

	$self->_fill_actions($ref_actions_preview, $graph);

	# maybe, we have nothing to do?
	return undef if scalar $graph->vertices() == 0;

	# fill the actions' dependencies
	foreach my $ref_inner_action ($graph->vertices()) {
		my $version = $ref_inner_action->{'version'};
		given ($ref_inner_action->{'action_name'}) {
			when ('unpack') {
				# if the package has pre-depends, they needs to be satisfied before
				# unpack (policy 7.2)
				# pre-depends must be unpacked before
				$self->_fill_action_dependencies(
						$version->{pre_depends}, 'configure', 'before', $ref_inner_action, $graph);
				# conflicts must be unsatisfied before
				$self->_fill_action_dependencies(
						$version->{conflicts}, 'remove', 'before', $ref_inner_action, $graph);
			}
			when ('configure') {
				# depends must be configured before
				$self->_fill_action_dependencies(
						$version->{depends}, 'configure', 'before', $ref_inner_action, $graph);
				# breaks must be unsatisfied before
				$self->_fill_action_dependencies(
						$version->{breaks}, 'remove', 'before', $ref_inner_action, $graph);

				# it has also to be unpacked if the same version was not in state 'unpacked'
				# search for the appropriate unpack action
				my %candidate_action = %$ref_inner_action;
				$candidate_action{'action_name'} = 'unpack';
				my $is_unpack_action_found = 0;
				foreach my $ref_current_action ($graph->vertices()) {
					if (__is_inner_actions_equal(\%candidate_action, $ref_current_action)) {
						# found...
						$graph->add_edge($ref_current_action, $ref_inner_action);
						$is_unpack_action_found = 1;
						last;
					}
				}

				if (!$is_unpack_action_found) {
					# pre-depends must be configured before
					$self->_fill_action_dependencies(
							$version->{pre_depends}, 'configure', 'before', $ref_inner_action, $graph);
					# conflicts must be unsatisfied before
					$self->_fill_action_dependencies(
							$version->{conflicts}, 'remove', 'before', $ref_inner_action, $graph);
				}
			}
			when ('remove') {
				# pre-depends must be removed after
				$self->_fill_action_dependencies(
						$version->{pre_depends}, 'remove', 'after', $ref_inner_action, $graph);
				# depends must be removed after
				$self->_fill_action_dependencies(
						$version->{depends}, 'remove', 'after', $ref_inner_action, $graph);
			}
		}
	}

	do { # unit all downgrades/upgrades
		# list of packages affected
		my @package_names_affected;
		push @package_names_affected, map { $_->{'package_name'} } @{$ref_actions_preview->{'install'}};
		push @package_names_affected, map { $_->{'package_name'} } @{$ref_actions_preview->{'upgrade'}};
		push @package_names_affected, map { $_->{'package_name'} } @{$ref_actions_preview->{'downgrade'}};

		# { $package_name => { 'from_remove' => $vertex || undef, 'from_unpack' => $vertex, 'to' => $vertex } }
		my %vertex_changes = map { $_ => {} } @package_names_affected;

		# pre-fill the list of downgrades/upgrades vertices
		foreach my $ref_inner_action ($graph->vertices()) {
			my $package_name = $ref_inner_action->{'version'}->{package_name};
			if (exists $vertex_changes{$package_name}) {
					$vertex_changes{$package_name}->{$ref_inner_action->{'action_name'}} = $ref_inner_action;
			}
		}

		my $sub_is_eaten_dependency = sub {
			my ($slave_vertex, $master_vertex, $version_to_install) = @_;

			if ($graph->has_edge_attributes($slave_vertex, $master_vertex)) {
				my ($ref_relation_expressions) = $graph->get_edge_attribute_values($slave_vertex, $master_vertex);
				RELATION_EXPRESSION:
				foreach my $relation_expression (@$ref_relation_expressions) {
					my $ref_satisfying_versions = $self->{_cache}->get_satisfying_versions($relation_expression);
					foreach my $version (@$ref_satisfying_versions) {
						if ($version->{package_name} eq $version_to_install->{package_name} &&
							$version->{version_string} eq $version_to_install->{version_string})
						{
							next RELATION_EXPRESSION;
						}
					}
					# no version can satisfy this relation expression
					return 0;
				}
				# all relation expressions are satisfying by some versions
				return 1;
			}
			# no relation expressions info, cannot be eaten
			return 0;
		};

		my $sub_move_edge = sub {
			my ($from_predecessor, $from_successor, $to_predecessor, $to_successor) = @_;

			my $ref_attributes = $graph->get_edge_attributes($from_predecessor, $from_successor);
			$graph->delete_edge($from_predecessor, $from_successor);
			my $ref_previous_attributes = $graph->get_edge_attributes($to_predecessor, $to_successor);
			if (exists $ref_previous_attributes->{'relation_expressions'}) {
				push @{$ref_attributes->{'relation_expressions'}},
						@{$ref_previous_attributes->{'relation_expressions'}};
			}
			$graph->add_edge($to_predecessor, $to_successor);
			$graph->set_edge_attributes($to_predecessor, $to_successor, $ref_attributes);
		};

		# unit remove/unpack unconditionally
		foreach my $ref_change_entry (values %vertex_changes) {
			my $from = $ref_change_entry->{'remove'};
			defined $from or next;
			my $to = $ref_change_entry->{'unpack'};

			for my $successor_vertex ($graph->successors($from)) {
				$sub_move_edge->($from, $successor_vertex, $to, $successor_vertex);
			}
			for my $predecessor_vertex ($graph->predecessors($from)) {
				$sub_move_edge->($predecessor_vertex, $from, $predecessor_vertex, $to);
			}
			$graph->delete_vertex($from);
		}

		# unit unpack/configure using some intelligence
		foreach my $ref_change_entry (values %vertex_changes) {
			my $to = $ref_change_entry->{'configure'};
			my $version = $to->{'version'};
			my $from = $ref_change_entry->{'unpack'};

			my @potential_edge_moves;
			my $some_dependencies_can_be_eaten = 0;

			for my $successor_vertex ($graph->successors($from)) {
				if (!$sub_is_eaten_dependency->($from, $successor_vertex, $version)) {
					push @potential_edge_moves, [ $from, $successor_vertex, $to, $successor_vertex ];
				} else {
					if ($self->{_config}->var('debug::worker')) {
						my $slave_action_string = __stringify_inner_action($from);
						my $master_action_string = __stringify_inner_action($successor_vertex);
						mydebug("ate action dependency: '$slave_action_string' -> '$master_action_string'");
					}
					$some_dependencies_can_be_eaten = 1;
				}
			}
			for my $predecessor_vertex ($graph->predecessors($from)) {
				if (!$sub_is_eaten_dependency->($predecessor_vertex, $from, $version)) {
					push @potential_edge_moves, [ $predecessor_vertex, $from, $predecessor_vertex, $to ];
				} else {
					if ($self->{_config}->var('debug::worker')) {
						my $slave_action_string = __stringify_inner_action($predecessor_vertex);
						my $master_action_string = __stringify_inner_action($from);
						mydebug("ate action dependency: '$slave_action_string' -> '$master_action_string'");
					}
					$some_dependencies_can_be_eaten = 1;
				}
			}

			# now, finally...
			# when we don't merge unpack and configure, this leads to dependency loops
			# when we merge unpack and configure always, this also leads to dependency loops
			# then try to merge only if the merge can drop extraneous dependencies
			if ($some_dependencies_can_be_eaten) {
				foreach my $ref_edge_move (@potential_edge_moves) {
					$sub_move_edge->(@$ref_edge_move);
				}
				$graph->delete_vertex($from);
				$to->{'action_name'} = 'install';
			}
			if ($self->{_config}->var('debug::worker')) {
				my $action_string = __stringify_inner_action($to);
				my $yes_no = $some_dependencies_can_be_eaten ? '' : 'not ';
				mydebug("${yes_no}merging action '$action_string'");
			}
		}
	};

	return $graph->strongly_connected_graph();
}

sub __split_heterogeneous_actions (@) {
	my @action_group_list = @_;

	my @new_action_group_list;
	foreach my $ref_action_group (@action_group_list) {
		# all the actions will have the same action name by algorithm
		my $action_name = $ref_action_group->[0]->{'action_name'};
		if (grep { $_->{'action_name'} ne $action_name } @$ref_action_group) {
			# heterogeneous actions
			my %subgroups = ('remove' => [], 'unpack' => [], 'install' => [], 'configure' => []);

			# split by action names
			map { push @{$subgroups{$_->{'action_name'}}}, $_ } @$ref_action_group;

			# set needed dpkg flags to first action in action group
			if (@{$subgroups{'remove'}}) {
				$subgroups{'remove'}->[0]->{'dpkg_flags'} = ' --force-depends';
			}
			if (@{$subgroups{'unpack'}}) {
				$subgroups{'unpack'}->[0]->{'dpkg_flags'} = ' --force-depends --force-conflicts';
			}
			if (@{$subgroups{'install'}}) {
				$subgroups{'install'}->[0]->{'dpkg_flags'} = ' --force-depends --force-conflicts';
			}

			# pushing by one
			foreach my $subgroup (@subgroups{'remove','unpack','install','configure'}) {
				# push if there are some actions in group
				push @new_action_group_list, $subgroup if @$subgroup;
			}
		} else {
			push @new_action_group_list, $ref_action_group;
		}
	}
	return @new_action_group_list;
}

sub _prepare_downloads ($$) {
	my ($self, $ref_actions_preview, $download_progress) = @_;

	my $archives_location = $self->_get_archives_location();

	my @pending_downloads;

	foreach my $user_action ('install', 'upgrade', 'downgrade') {
		my $ref_package_entries = $ref_actions_preview->{$user_action};
		foreach my $version (map { $_->{'version'} } @$ref_package_entries) {
			my $package_name = $version->{package_name};
			my $version_string = $version->{version_string};

			# for now, take just first URI
			my @uris = $version->uris();
			while ($uris[0] eq "") {
				# no real URI, just installed, skip it
				shift @uris;
			}
			# we need at least one real URI
			scalar @uris or
					mydie("no available download URIs for %s %s", $package_name, $version_string);

			# target path
			my $basename = __get_archive_basename($version);
			my $download_filename = $archives_location . $_download_partial_suffix . '/' . $basename;
			my $target_filename = $archives_location . '/' . $basename;

			# exclude from downloading packages that are already present
			next if (-e $target_filename && __verify_hash_sums($version, $target_filename));

			push @pending_downloads, {
				'uris' => [ map { $_->{'download_uri' } } @uris ],
				'filename' => $download_filename,
				'size' => $version->{size},
				'post-action' => sub {
					__verify_hash_sums($version, $download_filename) or
							do { unlink $download_filename; return sprintf __("%s: hash sums mismatch"), $download_filename; };
					move($download_filename, $target_filename) or
							return sprintf __("%s: unable to move target file: %s"), $download_filename, $!;

					# return success
					return 0;
				},
			};
			foreach my $uri (@uris) {
				my $download_uri = $uri->{'download_uri'};

				$download_progress->set_short_alias_for_uri($download_uri, $package_name);
				my $ref_release = $version->{avail_as}->[0]->{'release'};
				my $codename = $ref_release->{'codename'};
				my $component = $ref_release->{'component'};
				my $base_uri = $uri->{'base_uri'};
				$download_progress->set_long_alias_for_uri($download_uri,
						"$base_uri $codename/$component $package_name $version_string");
			}
		}
	}

	return @pending_downloads;
}

sub _do_downloads ($$$) {
	my ($self, $ref_pending_downloads, $download_progress) = @_;

	if ($self->{_config}->var('cupt::worker::simulate')) {
		foreach (@$ref_pending_downloads) {
			say __("downloading") . ": " . join(' | ', @{$_->{'uris'}});
		}
	} else {
		# don't bother ourselves with download preparings if nothing to download
		if (scalar @$ref_pending_downloads) {
			my @download_list;

			my $archives_location = $self->_get_archives_location();

			sysopen(LOCK, $archives_location . '/lock', O_WRONLY | O_CREAT, O_EXCL) or
					mydie("unable to open archives lock file: %s", $!);

			my $download_size = sum map { $_->{'size'} } @$ref_pending_downloads;
			$download_progress->set_total_estimated_size($download_size);

			my $download_result;
			do {
				my $download_manager = new Cupt::Download::Manager($self->{_config}, $download_progress);
				$download_result = $download_manager->download(@$ref_pending_downloads);
			}; # make sure that download manager is already destroyed at this point

			close(LOCK) or
					mydie("unable to close archives lock file: %s", $!);

			# fail and exit if it was something bad with downloading
			mydie($download_result) if $download_result;
		}
	}
}

sub _generate_stdin_for_preinstall_hooks_version2 ($$) {
	# how great is to write that "apt-listchanges uses special pipe from
	# apt" and document nowhere the format of this pipe, so I have to look
	# through the Python sources (I don't know Python btw) to determine
	# what the hell should I put to STDIN to satisfy apt-listchanges
	my ($self, $ref_action_group_list) = @_;
	my $result = '';
	$result .= "VERSION 2\n";

	do { # writing out a configuration
		my $config = $self->{_config};

		my $print_key_value = sub {
			my $key = shift;
			my $value = shift;
			defined $value or return;
			$result .= qq/$key="$value"\n/;
		};

		my @regular_keys = sort keys %{$config->{regular_vars}};
		foreach my $key (@regular_keys) {
			$print_key_value->($key, $config->{regular_vars}->{$key});
		}

		my @list_keys = sort keys %{$config->{list_vars}};
		foreach my $key (@list_keys) {
			my @values = @{$config->{list_vars}->{$key}};
			foreach (@values) {
				$print_key_value->("${key}::", $_);
			}
		}

		$result .= "\n";
	};
	foreach my $ref_action_group (@$ref_action_group_list) {
		my $action_name = $ref_action_group->[0]->{'action_name'};

		foreach my $ref_action (@$ref_action_group) {
			my $action_version = $ref_action->{'version'};
			my $package_name = $action_version->{package_name};
			my $old_version_string =
					$self->{_cache}->get_system_state()->get_installed_version_string($package_name) // '-';
			my $new_version_string = $action_name eq 'remove' ? '-' : $action_version->{version_string};

			my $compare_version_strings_sign;
			if ($old_version_string eq '-') {
				$compare_version_strings_sign = '<';
			} elsif ($new_version_string eq '-') {
				$compare_version_strings_sign = '>';
			} else {
				my $compare_result = Cupt::Core::compare_version_strings($old_version_string, $new_version_string);
				given ($compare_result) {
					when (-1) { $compare_version_strings_sign = '<' }
					when (0) { $compare_version_strings_sign = '=' }
					when (1) { $compare_version_strings_sign = '>' }
				}
			}
			my $filename;
			if ($action_name eq 'configure') {
				# special case for that
				$filename = "**CONFIGURE**";
			} elsif ($action_name eq 'remove') {
				# and for this...
				$filename = "**REMOVE**";
			} else { # unpack or install
				$filename = $self->_get_archives_location() . '/' . __get_archive_basename($action_version);
			}
			$result .= "$package_name $old_version_string < $new_version_string $filename\n";
			if ($action_name eq 'install') {
				$result .= "$package_name $old_version_string < $new_version_string **CONFIGURE**\n";
			}
		}
	}

	# strip last "\n", because apt-listchanges cannot live with it somewhy
	chop($result);

	return $result;
}

sub _run_external_command ($$$) {
	my ($self, $flavor, $command, $alias) = @_;

	if ($self->{_config}->var('cupt::worker::simulate')) {
		say __("simulating"), ": $command";
	} else {
		# invoking command
		system($command) == 0 or
				mydie("dpkg '%s' action '%s' returned non-zero status: %s", $flavor, $alias, $?);
	}
}

sub _do_dpkg_pre_actions ($$$) {
	my ($self, $ref_actions_preview, $ref_action_group_list) = @_;

	my $archives_location = $self->_get_archives_location();

	foreach my $command ($self->{_config}->var('dpkg::pre-invoke')) {
		$self->_run_external_command('pre', $command, $command);
	}
	foreach my $command ($self->{_config}->var('dpkg::pre-install-pkgs')) {
		my ($command_binary) = ($command =~ m/^(.*?)(?: |$)/);
		my $stdin;

		my $version_of_stdin = $self->{_config}->var("dpkg::tools::options::${command_binary}::version");
		my $alias = $command;
		if (defined $version_of_stdin and $version_of_stdin eq '2') {
			$stdin = $self->_generate_stdin_for_preinstall_hooks_version2($ref_action_group_list);
		} else {
			$stdin = '';
			# new debs are pulled to command through STDIN, one by line
			foreach my $action ('install', 'upgrade', 'downgrade') {
				foreach my $ref_entry (@{$ref_actions_preview->{$action}}) {
					my $version = $ref_entry->{'version'};
					my $deb_location = $archives_location . '/' .  __get_archive_basename($version);
					$stdin .= "$deb_location\n";
				}
			}
		}
		$command = "echo '$stdin' | $command";
		$self->_run_external_command('pre', $command, $alias);
	}
}

sub _do_dpkg_post_actions ($) {
	my ($self) = @_;
	foreach my $command ($self->{_config}->var('dpkg::post-invoke')) {
		$self->_run_external_command('post', $command, $command);
	}
}

=head2 change_system

member function, performes planned actions

Returns true if successful, false otherwise

Parameters:

I<download_progress> - reference to subclass of Cupt::Download::Progress

=cut

sub change_system ($$) {
	my ($self, $download_progress) = @_;

	my $simulate = $self->{_config}->var('cupt::worker::simulate');
	my $download_only = $self->{_config}->var('cupt::worker::download-only');

	my $dpkg_lock_fh;
	if (!$simulate && !$download_only) {
		sysopen($dpkg_lock_fh, '/var/lib/dpkg/lock', O_WRONLY | O_EXCL) or
				mydie("unable to open dpkg lock file: %s", $!);
	}

	my $ref_actions_preview = $self->get_actions_preview();

	my @pending_downloads = $self->_prepare_downloads($ref_actions_preview, $download_progress);
	$self->_do_downloads(\@pending_downloads, $download_progress);
	return 1 if $download_only;

	my $action_graph = $self->_build_actions_graph($ref_actions_preview);
	# exit when nothing to do
	defined $action_graph or return 1;
	# topologically sorted actions
	my @sorted_graph_vertices = $action_graph->topological_sort();
	my @action_group_list;
	foreach my $action_vertice (@sorted_graph_vertices) {
		push @action_group_list, $action_graph->get_vertex_attribute($action_vertice, 'subvertices');
	}
	@action_group_list = __split_heterogeneous_actions(@action_group_list);

	# doing or simulating the actions
	my $dpkg_binary = $self->{_config}->var('dir::bin::dpkg');
	my $defer_triggers = $self->{_config}->var('cupt::worker::defer-triggers');

	$self->_do_dpkg_pre_actions($ref_actions_preview, \@action_group_list);

	my $archives_location = $self->_get_archives_location();
	foreach my $ref_action_group (@action_group_list) {
		# all the actions will have the same action name by algorithm
		my $action_name = $ref_action_group->[0]->{'action_name'};

		if ($action_name eq 'remove' && $self->{_config}->var('cupt::worker::purge')) {
			$action_name = 'purge';
		}

		do { # do auto status info manipulations
			my %packages_changed = map { $_->{'version'}->{package_name} => 1 } @$ref_action_group;
			my $auto_action; # 'markauto' or 'unmarkauto'
			if ($action_name eq 'install' || $action_name eq 'unpack') {
				$auto_action = 'markauto';
			} elsif ($action_name eq 'remove' || $action_name eq 'purge') {
				$auto_action = 'unmarkauto';
			}
			if (defined $auto_action) {
				my @package_names_to_mark;
				foreach my $package_name (@{$ref_actions_preview->{$auto_action}}) {
					if (exists $packages_changed{$package_name}) {
						push @package_names_to_mark, $package_name;
					}
				}

				if (scalar @package_names_to_mark) {
					# mark on unmark as autoinstalled
					$self->mark_as_automatically_installed($auto_action eq 'markauto', @package_names_to_mark);
				}
			}
		};

		do { # dpkg actions
			my $dpkg_command = "$dpkg_binary --$action_name";
			$dpkg_command .= ' --no-triggers' if $defer_triggers;
			# add necessary options if requested
			$dpkg_command .= $ref_action_group->[0]->{'dpkg_flags'} // "";

			foreach my $ref_action (@$ref_action_group) {
				my $action_expression;
				if ($action_name eq 'install' || $action_name eq 'unpack') {
					my $version = $ref_action->{'version'};
					$action_expression = $archives_location . '/' .  __get_archive_basename($version);
				} else {
					my $package_name = $ref_action->{'version'}->{package_name};
					$action_expression = $package_name;
				}
				$dpkg_command .= " $action_expression";
			}
			if ($simulate) {
				say __("simulating"), ": $dpkg_command";
			} else {
				# invoking command
				system($dpkg_command) == 0 or
						mydie("dpkg returned non-zero status: %s", $?);
			}
		};
	}
	my $dpkg_pending_triggers_command = "$dpkg_binary --triggers-only --pending";
	if (!$simulate) {
		if ($defer_triggers) {
			# triggers were not processed during actions perfomed before, do it now at once
			system($dpkg_pending_triggers_command) == 0 or
					mydie("error processing triggers");
		}

		close($dpkg_lock_fh) or
				mydie("unable to close dpkg lock file: %s", $!);
	} else {
		if ($defer_triggers) {
			say __("simulating"), ": $dpkg_pending_triggers_command";
		}
	}

	$self->_do_dpkg_post_actions();

	return 1;
}

=head2 update_release_data

member function, performes update of APT/Cupt indexes

Returns true if successful, false otherwise

Parameters:

I<download_progress> - reference to subclass of Cupt::Download::Progress

=cut

sub update_release_data ($$) {
	my ($self, $download_progress) = @_;

	my $sub_stringify_index_entry = sub {
		my ($index_entry) = @_;

		return sprintf "%s %s/%s", $index_entry->{'uri'},
				$index_entry->{'distribution'}, $index_entry->{'component'};
	};

	my $cache = $self->{_cache};
	my @index_entries = @{$cache->get_index_entries()};

	my $download_manager = new Cupt::Download::Manager($self->{_config}, $download_progress);

	my @pids;
	foreach my $index_entry (@index_entries) {
		my $pid = fork() //
				mydie("unable to fork: $!");

		if ($pid) {
			# master process
			push @pids, $pid;
		} else {
			# child
			my $release_local_path = $cache->get_path_of_release_list($index_entry);
			my $release_download_uri = $cache->get_download_uri_of_release_list($index_entry);
			my $release_signature_download_uri = "$release_download_uri.gpg";
			say "local: $release_local_path, remote: $release_download_uri";
			my $download_result = $download_manager->download(
					{ 'uris' => [ $release_download_uri ], 'filename' => $release_local_path });
			if ($download_result) {
				# failed to download
				mywarn("failed to download index for '%s'", $sub_stringify_index_entry($index_entry);
			}

			CHILD_EXIT:
			POSIX::_exit(0);
		}
	}
	foreach my $pid (@pids) {
		waitpid $pid, 0;
	}
}

1;

