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

use Cupt::Core;
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
	return $self;
}

=head2 set_desired_state

method, sets desired state of the system

Parameters:

I<desired_state> - the desired state after the actions, hash reference:

{ I<package_name> => { 'version' => I<version>, 'manually_selected' => 1 } }

where:

I<version> - reference to L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion>

'manually_selected' is optional key

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

I<packages> = [ { 'package_name' => $package_name, 'version' => I<version> }... ]

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
			$ref_entry->{package_name} = $package_name;
			$ref_entry->{version} = $supposed_version;
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
	return ($ref_left_action->{'package_name'} eq $ref_right_action->{'package_name'} &&
			$ref_left_action->{'version_string'} eq $ref_right_action->{'version_string'} &&
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
				#foreach my $package_name (map { $_->{package_name} } @{$ref_actions_preview->{$user_action}}) {
				my $package_name = $ref_package_entry->{package_name};
				my $version_string;
				if ($inner_action eq 'remove') {
					$version_string = $self->{_system_state}->get_installed_version_string($package_name);
				} else {
					$version_string = $ref_package_entry->{'version'}->{version_string};
				}
				$graph->add_vertex({
						'package_name' => $package_name,
						'version_string' => $version_string,
						'action_name' => $inner_action,
				});
			}
		}
	}
}

# fills ref_graph with dependencies specified in ref_relations_expressions
sub _fill_action_dependencies ($$$$) {
	my ($self, $ref_relation_expressions, $action_name, $direction, $ref_inner_action, $graph) = @_;

	foreach my $relation_expression (@$ref_relation_expressions) {
		my $ref_satisfying_versions = $self->{_cache}->get_satisfying_versions($relation_expression);

		SATISFYING_VERSIONS:
		foreach my $other_version (@$ref_satisfying_versions) {
			my $other_package_name = $other_version->{package_name};

			my $other_version_string = $other_version->{version_string};
			my %candidate_action = (
				'package_name' => $other_package_name,
				'version_string' => $other_version_string,
				'action_name' => $action_name
			);
			# search for the appropriate action in action list
			foreach my $ref_current_action ($graph->vertices()) {
				if (__is_inner_actions_equal(\%candidate_action, $ref_current_action)) {
					# it's it!
					my $ref_master_action = $direction eq 'after' ? $ref_current_action : $ref_inner_action;
					my $ref_slave_action = $direction eq 'after' ? $ref_inner_action : $ref_current_action;

					$graph->add_edge($ref_slave_action, $ref_master_action);

					if ($self->{_config}->var('debug::worker')) {
						my $slave_package_name = $ref_slave_action->{'package_name'};
						my $slave_version_string = $ref_slave_action->{'version_string'};
						my $slave_action_name = $ref_slave_action->{'action_name'};
						my $slave_string = "$slave_action_name $slave_package_name $slave_version_string";
						my $master_package_name = $ref_master_action->{'package_name'};
						my $master_version_string = $ref_master_action->{'version_string'};
						my $master_action_name = $ref_master_action->{'action_name'};
						my $master_string = "$master_action_name $master_package_name $master_version_string";
						mydebug("action dependency: $slave_string -> $master_string");
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
	my $graph = new Graph ('directed' => 1, 'refvertexed' => 1);

	$self->_fill_actions($ref_actions_preview, $graph);

	# maybe, we have nothing to do?
	return undef if scalar $graph->vertices() == 0;

	# fill the actions' dependencies
	foreach my $ref_inner_action ($graph->vertices()) {
		my $package_name = $ref_inner_action->{'package_name'};
		given ($ref_inner_action->{'action_name'}) {
			when ('unpack') {
				# if the package has pre-depends, they needs to be satisfied before
				# unpack (policy 7.2)
				my $desired_version = $self->{_desired_state}->{$package_name}->{version};
				# pre-depends must be unpacked before
				$self->_fill_action_dependencies(
						$desired_version->{pre_depends}, 'configure', 'before', $ref_inner_action, $graph);
				# conflicts must be unsatisfied before
				$self->_fill_action_dependencies(
						$desired_version->{conflicts}, 'remove', 'before', $ref_inner_action, $graph);
			}
			when ('configure') {
				# configure can be done only after the unpack of the same version
				my $desired_version = $self->{_desired_state}->{$package_name}->{version};

				# pre-depends must be configured before
				$self->_fill_action_dependencies(
						$desired_version->{pre_depends}, 'configure', 'before', $ref_inner_action, $graph);
				# depends must be configured before
				$self->_fill_action_dependencies(
						$desired_version->{depends}, 'configure', 'before', $ref_inner_action, $graph);
				# breaks must be unsatisfied before
				$self->_fill_action_dependencies(
						$desired_version->{breaks}, 'remove', 'before', $ref_inner_action, $graph);

				# it has also to be unpacked if the same version was not in state 'unpacked'
				# search for the appropriate unpack action
				my %candidate_action = %$ref_inner_action;
				$candidate_action{'action_name'} = 'unpack';
				foreach my $ref_current_action ($graph->vertices()) {
					if (__is_inner_actions_equal(\%candidate_action, $ref_current_action)) {
						# found...
						$graph->add_edge($ref_current_action, $ref_inner_action);
						last;
					}
				}
			}
			when ('remove') {
				# package dependencies can be removed only after removal of the package
				my $package = $self->{_cache}->get_binary_package($package_name);
				my $version_string = $ref_inner_action->{'version_string'};
				my $system_version = $package->get_specific_version($version_string);
				# pre-depends must be removed after
				$self->_fill_action_dependencies(
						$system_version->{pre_depends}, 'remove', 'after', $ref_inner_action, $graph);
				# depends must be removed after
				$self->_fill_action_dependencies(
						$system_version->{depends}, 'remove', 'after', $ref_inner_action, $graph);
			}
		}
	}

	do { # unit all downgrades/upgrades
		# list of packages affected
		my @package_names_affected;
		push @package_names_affected, map { $_->{'package_name'} } @{$ref_actions_preview->{'upgrade'}};
		push @package_names_affected, map { $_->{'package_name'} } @{$ref_actions_preview->{'downgrade'}};

		# { $package_name => { 'from' => $vertex, 'to' => $vertex } }
		my %vertex_changes = map { $_ => {} } @package_names_affected;

		# pre-fill the list of downgrades/upgrades vertices
		foreach my $ref_inner_action ($graph->vertices()) {
			my $package_name = $ref_inner_action->{'package_name'};
			if (exists $vertex_changes{$package_name}) {
				if ($ref_inner_action->{'action_name'} eq 'remove') {
					$vertex_changes{$package_name}->{'from'} = $ref_inner_action;
				} elsif ($ref_inner_action->{'action_name'} eq 'unpack') {
					$vertex_changes{$package_name}->{'to'} = $ref_inner_action;
				}
			}
		}

		# unit!
		foreach my $ref_change_entry (values %vertex_changes) {
			my $from = $ref_change_entry->{'from'};
			my $to = $ref_change_entry->{'to'};
			for my $successor_vertex ($graph->successors($from)) {
				$graph->add_edge($to, $successor_vertex);
			}
			for my $predecessor_vertex ($graph->predecessors($from)) {
				$graph->add_edge($predecessor_vertex, $to);
			}
			$graph->delete_vertex($from);
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
			my %subgroups = ('remove' => [], 'unpack' => [], 'configure' => []);

			# split by action names
			map { push @{$subgroups{$_->{'action_name'}}}, $_ } @$ref_action_group;

			# set needed dpkg flags to first action in action group
			if (@{$subgroups{'remove'}}) {
				$subgroups{'remove'}->[0]->{'dpkg_flags'} = ' --force-depends';
			}
			if (@{$subgroups{'unpack'}}) {
				$subgroups{'unpack'}->[0]->{'dpkg_flags'} = ' --force-depends';
			}

			# pushing by one
			foreach my $subgroup (@subgroups{'remove','unpack','configure'}) {
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
	my ($self, $ref_action_group_list, $download_progress) = @_;

	my $archives_location = $self->_get_archives_location();

	my @pending_downloads;

	foreach my $ref_action_group (@$ref_action_group_list) {
		# all the actions will have the same action name by algorithm
		my $action_name = $ref_action_group->[0]->{'action_name'};

		if ($action_name eq 'unpack') {
			# we have to download this package(s)
			foreach my $ref_action (@$ref_action_group) {
				my $package_name = $ref_action->{'package_name'};
				my $version_string = $ref_action->{'version_string'};
				my $package = $self->{_cache}->get_binary_package($package_name);
				my $version = $package->get_specific_version($version_string);
				# for now, take just first URI
				my @uris = $version->uris();
				while ($uris[0] eq "") {
					# no real URI, just installed, skip it
					shift @uris;
				}
				# we need at least one real URI
				scalar @uris or
						mydie("no available download URIs for %s %s", $package_name, $version_string);

				my $uri = $uris[0];

				# target path
				my $basename = __get_archive_basename($version);
				my $download_filename = $archives_location . $_download_partial_suffix . '/' . $basename;
				my $target_filename = $archives_location . '/' . $basename;

				# exclude from downloading packages that are already present
				next if (-e $target_filename && __verify_hash_sums($version, $target_filename));

				push @pending_downloads, {
					'uri' => $uri,
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
				$download_progress->set_short_alias_for_uri($uri, $package_name);
				my $ref_release = $version->{avail_as}->[0]->{'release'};
				my $codename = $ref_release->{'codename'};
				my $component = $ref_release->{'component'};
				$download_progress->set_long_alias_for_uri($uri,
						"$codename/$component $package_name $version_string");
			}
		}
	}

	return @pending_downloads;
}

sub _generate_stdin_for_apt_listchanges ($$) {
	# how great is to write that "apt-listchanges uses special pipe from
	# apt" and document nowhere the format of this pipe, so I have to look
	# through the Python sources (I don't know Python btw) to determine
	# what the hell should I put to STDIN to satisfy apt-listchanges
	#
	# also, it's great idea to have pluggable hooks with different formats,
	# so a package manager should know about every tool...
	my ($self, $ref_action_group_list) = @_;
	my $result = '';
	my $listchanges_version_string = $self->{_config}->var('dpkg::tools::options::/usr/bin/apt-listchanges::version');
	$result .= "VERSION $listchanges_version_string\n" if defined $listchanges_version_string;

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

		next if $action_name eq 'remove';
		foreach my $ref_action (@$ref_action_group) {
			my $package_name = $ref_action->{'package_name'};
			my $old_version_string = $self->{_cache}->get_system_state()->get_installed_version_string($package_name);
			next unless defined $old_version_string;
			my $new_version_string = $ref_action->{'version_string'};
			# if not an upgrade
			next if Cupt::Core::compare_version_strings($old_version_string, $new_version_string) != -1;
			my $filename;
			if ($action_name eq 'configure') {
				# apt_listchanges needs special case for that
				$filename = "**CONFIGURE**";
			} else {
				my $package = $self->{_cache}->get_binary_package($package_name);
				my $version = $package->get_specific_version($new_version_string);
				$filename = $self->_get_archives_location() . '/' . __get_archive_basename($version);
			}
			$result .= "$package_name $old_version_string < $new_version_string $filename\n";
		}
	}

	# strip last "\n", because apt-listchanges cannot live with it somewhy
	chop($result);

	return $result;
}

=head2 do_actions

member function, performes planned actions

Returns true if successful, false otherwise

Parameters:

I<download_progress> - reference to subclass of Cupt::Download::Progress

=cut

sub do_actions ($$) {
	my ($self, $download_progress) = @_;

	my $ref_actions_preview = $self->get_actions_preview();
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

	my @pending_downloads = $self->_prepare_downloads(\@action_group_list, $download_progress);

	my $simulate = $self->{_config}->var('cupt::worker::simulate');

	if ($simulate) {
		foreach (@pending_downloads) {
			say __("downloading") . ": " . $_->{'uri'};
		}
	} else {
		# don't bother ourselves with download preparings if nothing to download
		if (scalar @pending_downloads) {
			my @download_list;

			my $archives_location = $self->_get_archives_location();

			sysopen(LOCK, $archives_location . '/lock', O_WRONLY | O_CREAT, O_EXCL) or
					mydie("unable to open archives lock file: %s", $!);

			my $download_size = sum map { $_->{'size'} } @pending_downloads;
			$download_progress->set_total_estimated_size($download_size);

			my $download_manager = new Cupt::Download::Manager($self->{_config}, $download_progress);
			my $download_result = $download_manager->download(@pending_downloads);

			close(LOCK) or
					mydie("unable to close archives lock file: %s", $!);

			# fail and exit if it was something bad with downloading
			mydie($download_result) if $download_result;
		}
	}

	return 1 if $self->{_config}->var('cupt::worker::download-only');


	# doing or simulating the actions
	my $dpkg_binary = $self->{_config}->var('dir::bin::dpkg');
	my $defer_triggers = $self->{_config}->var('cupt::worker::defer-triggers');
	if (!$simulate) {
		sysopen(LOCK, '/var/lib/dpkg/lock', O_WRONLY | O_EXCL) or
				mydie("unable to open dpkg lock file: %s", $!);
	}

	my $archives_location = $self->_get_archives_location();
	# performing pre-install actions
	foreach my $command ($self->{_config}->var('dpkg::pre-install-pkgs')) {
		my $stdin;

		if ($command =~ /apt-listchanges/) {
			$stdin = $self->_generate_stdin_for_apt_listchanges(\@action_group_list);
		} else {
			$stdin = '';
			# debs are pulled to command through STDIN, one by line
			foreach my $ref_entry (@{$ref_actions_preview->{'upgrade'}}) {
				my $version = $ref_entry->{'version'};
				my $deb_location = $archives_location . '/' .  __get_archive_basename($version);
				$stdin .= "$deb_location\n";
			}
		}
		$command = "echo '$stdin' | $command";

		if ($simulate) {
			say __("simulating"), ": $command";
		} else {
			# invoking command
			system($command) == 0 or
					mydie("pre-install action returned non-zero status: %s", $?);
		}
	}

	foreach my $ref_action_group (@action_group_list) {
		# all the actions will have the same action name by algorithm
		my $action_name = $ref_action_group->[0]->{'action_name'};

		if ($action_name eq 'remove' && $self->{_config}->var('cupt::worker::purge')) {
			$action_name = 'purge';
		}

		do { # do auto status info manipulations
			my %packages_changed = map { $_->{'package_name'} => 1 } @$ref_action_group;
			my $auto_action; # 'markauto' or 'unmarkauto'
			if ($action_name eq 'unpack') {
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
				my $package_name = $ref_action->{'package_name'};
				if ($action_name eq 'unpack') {
					my $version_string = $ref_action->{'version_string'};
					my $package = $self->{_cache}->get_binary_package($package_name);
					my $version = $package->get_specific_version($version_string);
					$action_expression = $archives_location . '/' .  __get_archive_basename($version);
				} else {
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

		close(LOCK) or
				mydie("unable to close dpkg lock file: %s", $!);
	} else {
		if ($defer_triggers) {
			say __("simulating"), ": $dpkg_pending_triggers_command";
		}
	}

	return 1;
}

1;

