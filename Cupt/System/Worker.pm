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

use List::Util qw(sum max min);
use List::MoreUtils qw(uniq apply any none);
use File::Copy;
use File::Basename;
use File::stat ();
use POSIX;
use Digest;

use Cupt::Core;
use Cupt::Cache;
use Cupt::Cache::Relation qw(stringify_relation_expression stringify_relation_expressions);
use Cupt::Download::Manager;
use Cupt::Download::DebdeltaHelper;
use Cupt::Graph;
use Cupt::Graph::TransitiveClosure;
use Cupt::System::Worker::Lock;

my $_download_partial_suffix = '/partial';

use Cupt::LValueFields qw(_config _cache _system_state _desired_state _lock);

=head1 METHODS

=head2 new

creates new Cupt::System::Worker object

Parameters:

I<config> - reference to L<Cupt::Config|Cupt::Config>

I<cache> - reference to L<Cupt::Cache|Cupt::Cache>

=cut

sub new {
	my $class = shift;
	my $self = bless [] => $class;
	$self->_config = shift;
	$self->_cache = shift;
	$self->_system_state = $self->_cache->get_system_state();
	$self->_desired_state = undef;
	$self->_lock = Cupt::System::Worker::Lock->new($self->_config,
			$self->_config->var('dir') . $self->_config->var('cupt::directory::state') . '/lock');
	$self->_lock->obtain();

	$self->_synchronize_apt_compat_symlinks();

	return $self;
}

sub DESTROY {
	my ($self) = @_;

	$self->_lock->release();

	return;
}

sub _synchronize_apt_compat_symlinks ($) {
	my ($self) = @_;

	return if $self->_config->var('cupt::worker::simulate');

	my $archives_directory = $self->_get_archives_directory();
	my @debs = glob("$archives_directory/*.deb");
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

			my $corrected_deb = "$archives_directory/$corrected_basename";

			next if -e $corrected_deb;

			if ($corrected_basename ne $basename) {
				symlink $basename, $corrected_deb or
						mywarn("unable to create APT compatibility symbolic link '%s' -> '%s': %s",
								$corrected_deb, $basename, $!);
			}
		}
	}
	return;
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
	$self->_desired_state = $ref_desired_state;
	return;
}

sub _get_archives_directory ($) {
	my ($self) = @_;
	return $self->_config->var('dir') .
			$self->_config->var('dir::cache') . '/' .
			$self->_config->var('dir::cache::archives');
}

sub _get_indexes_directory ($) {
	my ($self) = @_;
	return $self->_config->var('dir') .
			$self->_config->var('dir::state') . '/' .
			$self->_config->var('dir::state::lists');
}

sub __get_archive_basename ($) {
	my ($binary_version) = @_;

	return $binary_version->package_name . '_' .
			$binary_version->version_string . '_' .
			$binary_version->architecture . '.deb';
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

	if (!defined $self->_desired_state) {
		myinternaldie('worker desired state is not given');
	}
	foreach my $package_name (keys %{$self->_desired_state}) {
		my $action;
		my $ref_supposed_package_entry = $self->_desired_state->{$package_name};
		my $supposed_version = $ref_supposed_package_entry->{'version'};
		my $ref_installed_info = $self->_system_state->get_installed_info($package_name);
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
					if ($ref_installed_info->{'version_string'} eq $supposed_version->version_string) {
						# the same version, but the package was in some interim state
						$action = 'configure';
					} else {
						# some interim state, but other version
						$action = 'install';
					}
				} else {
					# otherwise some package version is installed
					my $version_comparison_result = Cupt::Core::compare_version_strings(
							$supposed_version->version_string, $ref_installed_info->{'version_string'});

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
					if ($self->_config->var('cupt::worker::purge')) {
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

	my $archives_directory = $self->_get_archives_directory();
	my $total_bytes = 0;
	my $need_bytes = 0;
	my @ref_package_entries = map { @{$ref_actions_preview->{$_}} } qw(install upgrade downgrade);
	foreach my $ref_package_entry (@ref_package_entries) {
		my $version = $ref_package_entry->{'version'};
		my $size = $version->size;
		$total_bytes += $size;
		$need_bytes += $size; # for start
		my $basename = __get_archive_basename($version);
		my $path = $archives_directory . '/' . $basename;
		-e $path or next; # skip if the file is not present in the cache dir
		# skip if the file is not what we want
		Cupt::Cache::verify_hash_sums($version->export_hash_sums(), $path) or next;
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
		$result{$ref_package_entry->{'package_name'}} = $version->installed_size;
	}

	# remove/purge
	foreach my $ref_package_entry (@{$ref_actions_preview->{'remove'}}, @{$ref_actions_preview->{'purge'}}) {
		my $package_name = $ref_package_entry->{'package_name'};
		my $package = $self->_cache->get_binary_package($package_name);
		if (defined $package) {
			my $old_version = $package->get_installed_version();
			# config-files entries won't have installed size
			$result{$package_name} = - ($old_version->installed_size // 0);
		} else {
			# probably, it's purge of already non-existent package
			$result{$package_name} = 0;
		}
	}

	# upgrade/downgrade
	foreach my $ref_package_entry (@{$ref_actions_preview->{'upgrade'}}, @{$ref_actions_preview->{'downgrade'}}) {
		my $new_version = $ref_package_entry->{'version'};
		my $package_name = $ref_package_entry->{'package_name'};
		my $old_version = $self->_cache->get_binary_package($package_name)->get_installed_version();
		$result{$package_name} = $new_version->installed_size - $old_version->installed_size;
	}

	# deconfigure
	foreach my $ref_package_entry (@{$ref_actions_preview->{'deconfigure'}}) {
		my $package_name = $ref_package_entry->{'package_name'};
		if ($self->_config->var('dir::state::status') !~ m{/status$}) {
			mywarn("unable to determine installed size for package '%s'", $package_name);
			$result{$package_name} = 0;
		} else {
			(my $admindir = $self->_config->var('dir::state::status')) =~ s{/status$}{};
			$result{$package_name} = - qx/dpkg-query --admindir=$admindir -f '\${Installed-Size}' --show $package_name/;
		}
	}

	# configure is uninteresting, it doesn't change unpacked size in system
	foreach my $ref_package_entry (@{$ref_actions_preview->{'configure'}}) {
		$result{$ref_package_entry->{'package_name'}} = 0;
	}

	# installed sizes are specified in kibibytes, convert them to bytes
	apply { $_ *= 1024 } values %result;

	return \%result;
}

sub __is_inner_actions_equal ($$) {
	my ($ref_left_action, $ref_right_action) = @_;
	my $left_version = $ref_left_action->{'version'};
	my $right_version = $ref_right_action->{'version'};
	return ($left_version->package_name eq $right_version->package_name &&
			$left_version->version_string eq $right_version->version_string &&
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
					my $package_name = $ref_package_entry->{'package_name'};
					my $package = $self->_cache->get_binary_package($package_name);
					if (defined $package) {
						# may be undef too in purge-only case
						$version = $package->get_installed_version();
					} else {
						# always purge-only
						$version = undef;
					}
				} else {
					$version = $ref_package_entry->{'version'};
				}
				$graph->add_vertex({
					'version' => $version // {
						package_name => $ref_package_entry->{'package_name'},
						version_string => '<dummy>',
					},
					'action_name' => defined $version ? $inner_action : 'purge-config-files',
				});
			}
		}
	}
	return;
}

sub __stringify_inner_action ($) {
	my ($ref_action) = @_;

	my $prefix = exists $ref_action->{'fake'} ? '(fake) ' : '';
	my $package_name = $ref_action->{'version'}->package_name;
	my $version_string = $ref_action->{'version'}->version_string;
	my $action_name = $ref_action->{'action_name'};
	return "$prefix$action_name $package_name $version_string";
}

# fills ref_graph with dependencies specified in ref_relations_expressions
sub _fill_action_dependencies ($$$$) {
	my ($self, $version, $dependency_name, $action_name, $direction, $ref_inner_action, $graph) = @_;

	foreach my $relation_expression (@{$version->$dependency_name}) {
		my $ref_satisfying_versions = $self->_cache->get_satisfying_versions($relation_expression);

		SATISFYING_VERSIONS:
		foreach my $other_version (@$ref_satisfying_versions) {
			my %candidate_action = (
				'version' => $other_version,
				'action_name' => $action_name
			);
			# search for the appropriate action in action list
			my $ref_search_index = $graph->get_graph_attribute('vertexes_by_package_name');
			foreach my $ref_current_action (@{$ref_search_index->{$other_version->package_name}}) {
				next if exists $ref_inner_action->{'fake'} and exists $ref_current_action->{'fake'};
				if (__is_inner_actions_equal(\%candidate_action, $ref_current_action)) {
					# it's it!
					my $ref_master_action = $direction eq 'after' ? $ref_current_action : $ref_inner_action;
					my $ref_slave_action = $direction eq 'after' ? $ref_inner_action : $ref_current_action;

					$graph->add_edge($ref_slave_action, $ref_master_action);

					if ($dependency_name ne 'conflicts' and $dependency_name ne 'breaks') {
						# passing the above if means that this edge was not originated from conflicts/breaks
						# so it deserves a chance to be eaten in the end, the while the conflicts/breaks edges are
						# definitely not a candidates

						# adding relation to attributes
						my $ref_relation_expressions = $graph->get_edge_attribute($ref_slave_action, $ref_master_action,
								'relation_expressions') // [];
						push @{$ref_relation_expressions}, $relation_expression;
						$graph->set_edge_attribute($ref_slave_action, $ref_master_action,
								'relation_expressions' => $ref_relation_expressions);
					} else {
						# well, set this property to make sure that action dependency will never be eaten
						$graph->set_edge_attribute($ref_slave_action, $ref_master_action, 'poisoned' => 1);
					}
					if ($dependency_name eq 'pre_depends') {
						$graph->set_edge_attribute($ref_slave_action, $ref_master_action, 'pre-dependency' => 1);
					}

					if ($self->_config->var('debug::worker')) {
						my $slave_string = __stringify_inner_action($ref_slave_action);
						my $master_string = __stringify_inner_action($ref_master_action);
						mydebug("new action dependency: '$slave_string' -> '$master_string'");
					}

					last SATISFYING_VERSIONS;
				}
			}
		}
	}
	return;
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
	my $simulate = $self->_config->var('cupt::worker::simulate');

	if ($simulate) {
		foreach my $package_name (@package_names) {
			my $prefix = $markauto ?
					__('marking as automatically installed') : __('marking as manually installed');
			mysimulate('%s: %s', $prefix, $package_name);
		}
	} else {
		my $ref_extended_info = $self->_cache->get_extended_info();

		my $ref_autoinstalled_packages = $ref_extended_info->{'automatically_installed'};
		foreach my $package_name (@package_names) {
			$ref_autoinstalled_packages->{$package_name} = $markauto;
		}

		my @refreshed_autoinstalled_packages = grep { $ref_autoinstalled_packages->{$_} }
				keys %$ref_autoinstalled_packages;

		my $extended_info_file = $self->_cache->get_path_of_extended_states();
		my $temp_file = $extended_info_file . '.cupt.tmp';

		sysopen(my $temp_fh, $temp_file, O_WRONLY | O_EXCL | O_CREAT) or
				mydie("unable to open temporary file '%s': %s", $temp_file, $!);

		# filling new info
		foreach my $package_name (@refreshed_autoinstalled_packages) {
			print { $temp_fh } "Package: $package_name\nAuto-Installed: 1\n\n" or
					mydie("unable to write to file '%s': %s", $temp_file, $!);
		}

		close($temp_fh) or
				mydie("unable to close temporary file '%s': %s", $temp_file, $!);
		move($temp_file, $extended_info_file) or
				mydie("unable to rename temporary file '%s' to extended states file '%s': %s",
						$temp_file, $extended_info_file, $!);
	}
	return;
}

sub _build_actions_graph ($$) {
	my ($self, $ref_actions_preview) = @_;

	if (!defined $self->_desired_state) {
		myinternaldie('worker desired state is not given');
	}

	# action = {
	# 	'package_name' => package
	# 	'version_string' => version_string,
	# 	'action_name' => ('unpack' | 'configure' | 'remove')
	# }
	my $graph = Cupt::Graph->new();

	$self->_fill_actions($ref_actions_preview, $graph);

	# maybe, we have nothing to do?
	return undef if scalar $graph->vertices() == 0;

	# here we also adding adding fake-antiupgrades for all packages in the system
	# which stay unmodified for the sake of getting inter-dependencies between
	# the optional dependecies like
	#
	# 'abc' depends 'x | y', abc stays unmodified, x goes away, y is going to be installed
	#
	# here, action 'remove x' are dependent on 'install y' one and it gets
	# introduced by
	#
	# 'install y' -> 'install abc' <-> 'remove abc' -> 'remove x'
	#                {----------------------------}
	#                            <merge>
	# which goes into
	#
	# 'install y' -> 'remove x'
	#
	# moreover, we split the installed version into virtual version by one
	# relation expression, so different relation expressions of the same real
	# version don't interact with each other, otherwise we'd get a full cyclic
	# mess
	my @virtual_edges_to_be_eliminated;
	do {
		my @blacklisted_package_names;
		# the black list
		foreach my $ref_inner_action ($graph->vertices()) {
			push @blacklisted_package_names, $ref_inner_action->{'version'}->package_name;
		}
		foreach my $version (@{$self->_cache->get_system_state()->export_installed_versions()}) {
			my $package_name = $version->package_name;
			next if any { $package_name eq $_ } @blacklisted_package_names;

			foreach my $dependency_name (qw(pre_depends depends)) {
				foreach my $relation_expression (@{$version->$dependency_name}) {
					my $virtual_version = (bless [] => 'Cupt::Cache::BinaryVersion');
					$virtual_version->package_name = "$package_name [" .
							stringify_relation_expression($relation_expression) . ']';
					$virtual_version->version_string = $version->version_string;
					$virtual_version->pre_depends = [];
					$virtual_version->depends = [];
					$virtual_version->$dependency_name = [ $relation_expression ];
					$virtual_version->conflicts = [];
					$virtual_version->breaks = [];

					my $from_vertex = { 'version' => $virtual_version, 'action_name' => 'configure', 'fake' => 1 };
					my $to_vertex = { 'version' => $virtual_version, 'action_name' => 'remove', 'fake' => 1 };
					# we don't add edge here, but add the vertexes to gain dependencies and
					# save the vertexes order
					$graph->add_vertex($from_vertex);
					$graph->add_vertex($to_vertex);
					push @virtual_edges_to_be_eliminated, [ $from_vertex, $to_vertex ];
				}
			}
		}
	};

	my %vertexes_by_package_name;
	do { # building the action index for fast action search
		# it will be used only for filling edges, when vertex content isn't changed
		foreach my $vertex ($graph->vertices()) {
			my $package_name = $vertex->{'version'}->package_name;
			push @{$vertexes_by_package_name{$package_name}}, $vertex;
		}
		$graph->set_graph_attribute('vertexes_by_package_name', \%vertexes_by_package_name);
	};

	# fill the actions' dependencies
	foreach my $ref_inner_action ($graph->vertices()) {
		my $version = $ref_inner_action->{'version'};
		given ($ref_inner_action->{'action_name'}) {
			when ('unpack') {
				# if the package has pre-depends, they needs to be satisfied before
				# unpack (policy 7.2)
				# pre-depends must be unpacked before
				$self->_fill_action_dependencies(
						$version, 'pre_depends', 'configure', 'before', $ref_inner_action, $graph);
				# conflicts must be unsatisfied before
				$self->_fill_action_dependencies(
						$version, 'conflicts', 'remove', 'before', $ref_inner_action, $graph);
			}
			when ('configure') {
				# depends must be configured before
				$self->_fill_action_dependencies(
						$version, 'depends', 'configure', 'before', $ref_inner_action, $graph);
				# breaks must be unsatisfied before
				$self->_fill_action_dependencies(
						$version, 'breaks', 'remove', 'before', $ref_inner_action, $graph);

				# it has also to be unpacked if the same version was not in state 'unpacked'
				# search for the appropriate unpack action
				my %candidate_action = %$ref_inner_action;
				$candidate_action{'action_name'} = 'unpack';
				my $is_unpack_action_found = 0;
				foreach my $ref_current_action (@{$vertexes_by_package_name{$version->package_name}}) {
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
							$version, 'pre_depends', 'configure', 'before', $ref_inner_action, $graph);
					# conflicts must be unsatisfied before
					$self->_fill_action_dependencies(
							$version, 'conflicts', 'remove', 'before', $ref_inner_action, $graph);
				}
			}
			when ('remove') {
				# pre-depends must be removed after
				$self->_fill_action_dependencies(
						$version, 'pre_depends', 'remove', 'after', $ref_inner_action, $graph);
				# depends must be removed after
				$self->_fill_action_dependencies(
						$version, 'depends', 'remove', 'after', $ref_inner_action, $graph);
				# conflicts may be satisfied only after
				$self->_fill_action_dependencies(
						$version, 'conflicts', 'unpack', 'after', $ref_inner_action, $graph);
				# breaks may be satisfied only after
				$self->_fill_action_dependencies(
						$version, 'breaks', 'configure', 'after', $ref_inner_action, $graph);
			}
		}
	}
	$graph->delete_graph_attributes();

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
			my $package_name = $ref_inner_action->{'version'}->package_name;

			if (exists $vertex_changes{$package_name}) {
					$vertex_changes{$package_name}->{$ref_inner_action->{'action_name'}} = $ref_inner_action;
			}
		}

		do { # process indirect upgrades
			my @merge_exception_package_names = $self->_config->var('cupt::worker::allow-indirect-upgrade');
			foreach my $merge_exception_package_name (@merge_exception_package_names) {
				my $ref_vertex_change = $vertex_changes{$merge_exception_package_name};
				if (defined $ref_vertex_change) {
					# there is some action with this package name
					if (defined $ref_vertex_change->{'remove'}) {
						# set a dependency before remove and unpack
						$graph->add_edge($ref_vertex_change->{'remove'}, $ref_vertex_change->{'unpack'});
					}
					delete $vertex_changes{$merge_exception_package_name};
				}
			}
		};

		my $sub_is_eaten_dependency = sub {
			my ($slave_vertex, $master_vertex, $version_to_install) = @_;

			return 0 if defined $graph->get_edge_attribute($slave_vertex, $master_vertex, 'poisoned');

			my $ref_relation_expressions = $graph->get_edge_attribute($slave_vertex, $master_vertex, 'relation_expressions');
			if (defined $ref_relation_expressions) {
				RELATION_EXPRESSION:
				foreach my $relation_expression (@$ref_relation_expressions) {
					my $ref_satisfying_versions = $self->_cache->get_satisfying_versions($relation_expression);
					foreach my $version (@$ref_satisfying_versions) {
						if ($version->package_name eq $version_to_install->package_name &&
							$version->version_string eq $version_to_install->version_string)
						{
							next RELATION_EXPRESSION;
						}
					}
					# no version can satisfy this relation expression
					return 0;
				}
				# all relation expressions are satisfying by some versions
				if ($self->_config->var('debug::worker')) {
					my $slave_action_string = __stringify_inner_action($slave_vertex);
					my $master_action_string = __stringify_inner_action($master_vertex);
					my $relation_expressions_string = stringify_relation_expressions($ref_relation_expressions);
					mydebug(sprintf "ate action dependency: '%s' -> '%s' using '%s'",
							$slave_action_string, $master_action_string, $relation_expressions_string);
				}
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
			if (exists $ref_previous_attributes->{'poisoned'}) {
				$ref_attributes->{'poisoned'} = 1;
			}
			if (exists $ref_previous_attributes->{'pre-dependency'}) {
				$ref_attributes->{'pre-dependency'} = 1;
			}
			$graph->add_edge($to_predecessor, $to_successor);
			$graph->set_edge_attributes($to_predecessor, $to_successor, $ref_attributes);
		};

		do { # get rid of virtual edges
			foreach (@virtual_edges_to_be_eliminated) {
				my ($from_vertex, $to_vertex) = @$_;

				# "multiplying" the dependencies
				foreach my $predecessor_vertex ($graph->predecessors($from_vertex)) {
					foreach my $successor_vertex ($graph->successors($to_vertex)) {
						# moving edge attributes too
						$sub_move_edge->($predecessor_vertex, $from_vertex, $predecessor_vertex, $successor_vertex);
						$sub_move_edge->($to_vertex, $successor_vertex, $predecessor_vertex, $successor_vertex);
						if ($self->_config->var('debug::worker')) {
							my $slave_string = __stringify_inner_action($predecessor_vertex);
							my $master_string = __stringify_inner_action($successor_vertex);
							my $mediator_package_name = $from_vertex->{'version'}->package_name;
							mydebug(sprintf "multiplied action dependency: '%s' -> '%s', virtual mediator: '%s'",
									$slave_string, $master_string, $mediator_package_name);
						}

					}
				}

				$graph->delete_vertex($from_vertex);
				$graph->delete_vertex($to_vertex);
			}
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

		my $graph_transitive_closure = Cupt::Graph::TransitiveClosure->new($graph);

		# unit unpack/configure using some intelligence
		foreach my $ref_change_entry (values %vertex_changes) {
			my $to = $ref_change_entry->{'configure'};
			my $version = $to->{'version'};
			my $from = $ref_change_entry->{'unpack'};

			my @potential_edge_moves;
			my $do_merge = 0;

			if ($graph_transitive_closure->is_reachable($to, $from)) {
				# cyclic ('remove/unpack' <-> 'configure') dependency, merge unconditionally
				$do_merge = 1;
			}

			for my $successor_vertex ($graph->successors($from)) {
				if (!$sub_is_eaten_dependency->($from, $successor_vertex, $version)) {
					push @potential_edge_moves, [ $from, $successor_vertex, $to, $successor_vertex ];
				} else {
					$do_merge = 1;
				}
			}
			for my $predecessor_vertex ($graph->predecessors($from)) {
				if (!$sub_is_eaten_dependency->($predecessor_vertex, $from, $version)) {
					push @potential_edge_moves, [ $predecessor_vertex, $from, $predecessor_vertex, $to ];
				} else {
					$do_merge = 1;
				}
			}

			# now, finally...
			# when we don't merge unpack and configure, this leads to dependency loops
			# when we merge unpack and configure always, this also leads to dependency loops
			# then try to merge only if the merge can drop extraneous dependencies
			if ($do_merge) {
				foreach my $ref_edge_move (@potential_edge_moves) {
					$sub_move_edge->(@$ref_edge_move);
				}
				$graph->delete_vertex($from);
				$to->{'action_name'} = 'install';
			}
			if ($self->_config->var('debug::worker')) {
				my $action_string = __stringify_inner_action($to);
				my $yes_no = $do_merge ? '' : 'not ';
				mydebug("${yes_no}merging action '$action_string'");
			}
		}

		do { # check pre-depends
			# re-compute transitive matrix, it might become invalid after the merges
			$graph_transitive_closure = Cupt::Graph::TransitiveClosure->new($graph);

			foreach my $edge ($graph->edges()) {
				if (defined $graph->get_edge_attribute(@$edge, 'pre-dependency')) {
					my ($from_vertex, $to_vertex) = @$edge;
					if ($graph_transitive_closure->is_reachable($to_vertex, $from_vertex)) {
						# bah! the pre-dependency cannot be overridden, it's a fatal error
						# which is not worker's fail (at least, it shouldn't be)

						# then, all pre-dependency edges should have at least one relation
						# expression attribute
						my $ref_attributes = $graph->get_edge_attributes($from_vertex, $to_vertex);
						if (not exists $ref_attributes->{'relation_expressions'}) {
							myinternaldie('pre-dependency edge has not relation expressions');
						}

						my @path = $graph_transitive_closure->path_vertices($to_vertex, $from_vertex);
						my @package_names_in_path = uniq map { $_->{'version'}->package_name } @path;

						mywarn("the pre-dependency(ies) '%s' will be broken during the actions, the packages involved: %s",
								stringify_relation_expressions($ref_attributes->{'relation_expressions'}),
								join(', ', map { qq/'$_'/ } @package_names_in_path));
					}
				}
			}
		};
	};

	return $graph;
}

sub __split_heterogeneous_actions (@) {
	my @action_group_list = @_;

	my @new_action_group_list;
	foreach my $ref_action_group (@action_group_list) {
		# all the actions will have the same action name by algorithm
		my $action_name = $ref_action_group->[0]->{'action_name'};
		if (any { $_->{'action_name'} ne $action_name } @$ref_action_group) {
			# heterogeneous actions
			my %subgroups = ('remove' => [], 'unpack' => [], 'install' => [], 'configure' => []);

			# split by action names
			foreach my $ref_action (@$ref_action_group) {
				push @{$subgroups{$ref_action->{'action_name'}}}, $ref_action;
			}

			# set needed dpkg flags to first action in action group
			if (@{$subgroups{'remove'}}) {
				$subgroups{'remove'}->[0]->{'dpkg_flags'} = ' --force-depends';
			}
			if (@{$subgroups{'unpack'}}) {
				$subgroups{'unpack'}->[0]->{'dpkg_flags'} = ' --force-depends --force-conflicts';
			}
			if (@{$subgroups{'install'}}) {
				$subgroups{'install'}->[0]->{'dpkg_flags'} = ' --force-depends';
			}

			# pushing by one
			foreach my $subgroup (@subgroups{'remove','unpack','install','configure'}) {
				# push if there are some actions in group
				push @new_action_group_list, $subgroup if @$subgroup;
			}

			# last subgroup definitely don't need an additional dpkg flags
			delete $new_action_group_list[-1]->[0]->{'dpkg_flags'};
		} else {
			push @new_action_group_list, $ref_action_group;
		}
	}

	# dpkg cannot handle multiple-actions-by-one request gracefully, caring
	# about all depends and only then dealing with packages, the only known
	# working case is installing packages depending on each other together.
	#
	# is it bug or not, but we should leave with it
	foreach my $ref_action_group (@new_action_group_list) {
		if (scalar @$ref_action_group > 1) {
			if (!exists $ref_action_group->[0]->{'dpkg_flags'}) {
				$ref_action_group->[0]->{'dpkg_flags'} = '';
			}
			if ($ref_action_group->[0]->{'dpkg_flags'} !~ m/force-depends/) {
				$ref_action_group->[0]->{'dpkg_flags'} .= ' --force-depends';
			}
			if ($ref_action_group->[0]->{'action_name'} eq 'unpack' ||
				$ref_action_group->[0]->{'action_name'} eq 'install')
			{
				if ($ref_action_group->[0]->{'dpkg_flags'} !~ m/force-conflicts/) {
					$ref_action_group->[0]->{'dpkg_flags'} .= ' --force-conflicts';
				}
			}
		}
	}

	return @new_action_group_list;
}

sub __move_unpacks_to_configures (@) {
	my ($ref_action_group_list, $action_graph) = @_;

	my @new_action_group_list = @$ref_action_group_list;

	my $sub_move = sub {
		my ($move_action_name) = @_;
		foreach my $index (0..$#new_action_group_list) {
			my $ref_action_group = $new_action_group_list[$index];
			# all the actions will have the same action name by algorithm
			my $action_name = $ref_action_group->[0]->{'action_name'};
			next if $action_name ne $move_action_name;
			if (any { $_->{'action_name'} ne $action_name } @$ref_action_group) {
				# heterogeneous actions, don't touch it
				next;
			}

			# ok, try to move it as left as possible
			my $try_index = $index - 1;
			TRY_INDEX:
			while ($try_index >= 0) {
				foreach my $ref_right_action (@$ref_action_group) {
					foreach my $ref_left_action (@{$new_action_group_list[$try_index]}) {
						my @edge = ($move_action_name eq 'configure') ?
								($ref_left_action, $ref_right_action) :
								($ref_right_action, $ref_left_action);
						if ($action_graph->has_edge(@edge)) {
							# cannot move
							last TRY_INDEX;
						}
					}
				}
				# move!
				@new_action_group_list[$try_index, $try_index+1] = @new_action_group_list[$try_index+1, $try_index];
				--$try_index;
			}
		}
	};

	@new_action_group_list = reverse @new_action_group_list;
	$sub_move->('unpack');
	@new_action_group_list = reverse @new_action_group_list;
	$sub_move->('configure');

	return @new_action_group_list;
}

sub _prepare_downloads ($$) {
	my ($self, $ref_actions_preview, $download_progress) = @_;

	my $archives_directory = $self->_get_archives_directory();

	unless ($self->_config->var('cupt::worker::simulate')) {
		# prepare partial directory if it doesn't exist
		my $partial_directory = "$archives_directory$_download_partial_suffix";
		if (! -e $partial_directory) {
			mkdir($partial_directory) or
					mydie("unable to create partial directory '%s'", $partial_directory);
		}
	};

	my @pending_downloads;

	my $debdelta_helper = Cupt::Download::DebdeltaHelper->new();

	foreach my $user_action ('install', 'upgrade', 'downgrade') {
		my $ref_package_entries = $ref_actions_preview->{$user_action};
		foreach my $version (map { $_->{'version'} } @$ref_package_entries) {
			my $package_name = $version->package_name;
			my $version_string = $version->version_string;

			# for now, take just first URI
			my @uris = $version->uris();
			while ($uris[0] eq '') {
				# no real URI, just installed, skip it
				shift @uris;
			}

			# try using debdelta if possible
			unshift @uris, $debdelta_helper->uris($version, $self->_cache);

			# we need at least one real URI
			scalar @uris or
					mydie('no available download URIs for %s %s', $package_name, $version_string);

			# target path
			my $basename = __get_archive_basename($version);
			my $download_filename = $archives_directory . $_download_partial_suffix . '/' . $basename;
			my $target_filename = $archives_directory . '/' . $basename;

			# exclude from downloading packages that are already present
			if (-e $target_filename && Cupt::Cache::verify_hash_sums($version->export_hash_sums(), $target_filename)) {
				next;
			}

			my @download_uri_entries;
			foreach my $uri (@uris) {
				my $download_uri = $uri->{'download_uri'};

				my $ref_release = $version->available_as->[0]->{'release'};
				my $codename = $ref_release->{'codename'};
				my $component = $ref_release->{'component'};
				my $base_uri = $uri->{'base_uri'};
				my $long_alias = "$base_uri $codename/$component $package_name $version_string";

				push @download_uri_entries, {
					'uri' => $download_uri,
					'short-alias' => $package_name,
					'long-alias' => $long_alias,
				};
			}

			push @pending_downloads, {
				'uri-entries' => \@download_uri_entries,
				'filename' => $download_filename,
				'size' => $version->size,
				'post-action' => sub {
					Cupt::Cache::verify_hash_sums($version->export_hash_sums(), $download_filename) or
							do {
								unlink $download_filename; ## no critic (RequireCheckedSyscalls)
								return sprintf __('%s: hash sums mismatch'), $download_filename;
							};
					move($download_filename, $target_filename) or
							return sprintf __('%s: unable to move target file: %s'), $download_filename, $!;

					# return success
					return 0;
				},
				# auxiliary
				'package_name' => $version->package_name,
				'target_filename' => $target_filename,
			};
		}
	}

	# sort alphabetically, just for easy
	@pending_downloads = sort { $a->{'package_name'} cmp $b->{'package_name'} } @pending_downloads;

	return @pending_downloads;
}

sub _do_downloads ($$$) {
	my ($self, $ref_pending_downloads, $download_progress) = @_;

	# don't bother ourselves with download preparings if nothing to download
	if (scalar @$ref_pending_downloads) {
		my $archives_directory = $self->_get_archives_directory();

		my $archives_lock = Cupt::System::Worker::Lock->new($self->_config, "$archives_directory/lock");
		$archives_lock->obtain();

		my $download_size = sum map { $_->{'size'} } @$ref_pending_downloads;
		$download_progress->set_total_estimated_size($download_size);

		my $download_result;
		do {
			my $download_manager = Cupt::Download::Manager->new($self->_config, $download_progress);
			$download_result = $download_manager->download(@$ref_pending_downloads);
		}; # make sure that download manager is already destroyed at this point

		$archives_lock->release();

		# fail and exit if it was something bad with downloading
		mydie('there were download errors') if $download_result;
	}
	return;
}

sub _clean_downloads ($$) {
	my ($self, $ref_downloads) = @_;

	my $archives_directory = $self->_get_archives_directory();
	my $archives_lock = Cupt::System::Worker::Lock->new($self->_config, "$archives_directory/lock");
	$archives_lock->obtain();

	foreach my $filename (map { $_->{'target_filename'} } @$ref_downloads) {
		if ($self->_config->var('cupt::worker::simulate')) {
			mysimulate("removing the archive '%s'", $filename);
		} else {
			unlink($filename) or
					mydie("unable to remove file '%s': %s", $filename, $!);
		}
	}

	$archives_lock->release();
	return;
}

sub _generate_stdin_for_preinstall_hooks_version2 ($$) {
	# all hate undocumented formats...
	my ($self, $ref_action_group_list) = @_;
	my $result = '';
	$result .= "VERSION 2\n";

	do { # writing out a configuration
		my $config = $self->_config;

		my $print_key_value = sub {
			my ($key, $value) = @_;
			defined $value or return;
			$result .= qq/$key=$value\n/;
		};

		my @regular_keys = sort keys %{$config->regular_vars};
		foreach my $key (@regular_keys) {
			$print_key_value->($key, $config->regular_vars->{$key});
		}

		my @list_keys = sort keys %{$config->list_vars};
		foreach my $key (@list_keys) {
			my @values = @{$config->list_vars->{$key}};
			foreach (@values) {
				$print_key_value->("${key}::", $_);
			}
		}

		$result .= "\n";
	};
	foreach my $ref_action_group (@$ref_action_group_list) {
		my $action_name = $ref_action_group->[0]->{'action_name'};
		next if $action_name eq 'purge-config-files';

		foreach my $ref_action (@$ref_action_group) {
			my $action_version = $ref_action->{'version'};
			my $package_name = $action_version->package_name;
			my $old_version_string =
					$self->_cache->get_system_state()->get_installed_version_string($package_name) // '-';
			my $new_version_string = $action_name eq 'remove' ? '-' : $action_version->version_string;

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
				$filename = '**CONFIGURE**';
			} elsif ($action_name eq 'remove') {
				# and for this...
				$filename = '**REMOVE**';
			} else { # unpack or install
				$filename = $self->_get_archives_directory() . '/' . __get_archive_basename($action_version);
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
	my ($self, $command, $error_identifier) = @_;
	$error_identifier //= $command;

	if ($self->_config->var('cupt::worker::simulate')) {
		mysimulate("running command '%s'", $command);
	} else {
		# invoking command
		system($command) == 0 or
				mydie('%s returned non-zero status: %s', $error_identifier, $?);
	}

	return;
}

sub _run_dpkg_command {
	my ($self, $flavor, $command, $alias) = @_;

	my $error_identifier = sprintf __("dpkg '%s' action '%s'"), $flavor, $alias;
	$self->_run_external_command($command, $error_identifier);

	return;
}

sub _do_dpkg_pre_actions ($$$) {
	my ($self, $ref_actions_preview, $ref_action_group_list) = @_;
	foreach my $command ($self->_config->var('dpkg::pre-invoke')) {
		$self->_run_dpkg_command('pre', $command, $command);
	}
	return;
}

sub _do_dpkg_pre_packages_actions {
	my ($self, $ref_action_group_list) = @_;

	my $archives_directory = $self->_get_archives_directory();
	foreach my $command ($self->_config->var('dpkg::pre-install-pkgs')) {
		my ($command_binary) = ($command =~ m/^(.*?)(?: |$)/);
		my $stdin;

		my $version_of_stdin = $self->_config->var("dpkg::tools::options::${command_binary}::version");
		my $alias = $command;
		if (defined $version_of_stdin and $version_of_stdin eq '2') {
			$stdin = $self->_generate_stdin_for_preinstall_hooks_version2($ref_action_group_list);
		} else {
			$stdin = '';
			# new debs are pulled to command through STDIN, one by line
			foreach my $ref_action_group (@$ref_action_group_list) {
				foreach my $ref_action (@$ref_action_group) {
					my $action_name = $ref_action->{'action_name'};
					next if $action_name ne 'install' and $action_name ne 'unpack';
					my $version = $ref_action->{'version'};
					my $deb_file = $archives_directory . '/' .  __get_archive_basename($version);
					$stdin .= "$deb_file\n";
				}
			}
		}
		$command = "echo '$stdin' | $command";
		$self->_run_dpkg_command('pre', $command, $alias);
	}
	return;
}

sub _do_dpkg_post_actions ($) {
	my ($self) = @_;
	foreach my $command ($self->_config->var('dpkg::post-invoke')) {
		$self->_run_dpkg_command('post', $command, $command);
	}
	return;
}

sub _split_action_group_list_into_changesets {
	my ($self, $ref_action_group_list, $ref_pending_downloads) = @_;

	# ok, need to produce several changesets
	my @result;
	my %unpacked_package_names;

	my @current_changeset;
	my @current_downloads;
	foreach my $ref_action_group (@$ref_action_group_list) {
		my $action_name = $ref_action_group->[0]->{'action_name'};
		my @package_names = map { $_->{'version'}->package_name } @$ref_action_group;

		given ($action_name) {
			when ('unpack') {
				$unpacked_package_names{$_} = 1 foreach @package_names;
			}
			when ('configure') {
				delete $unpacked_package_names{$_} foreach @package_names;
			}
		}

		push @current_changeset, $ref_action_group;

		if ($action_name eq 'unpack' or $action_name eq 'install') {
			foreach my $package_name (@package_names) {
				foreach my $ref_download_entry (@$ref_pending_downloads) {
					if ($ref_download_entry->{'package_name'} eq $package_name) {
						# need to download that package
						push @current_downloads, $ref_download_entry;
						last; # download entry
					}
				}
			}
		}

		if (not %unpacked_package_names) {
			# all are configured, end of changeset
			push @result, {
				'action_group_list' => [ @current_changeset ],
				'downloads' => [ @current_downloads ],
			};

			@current_changeset = ();
			@current_downloads = ();
		}
	}

	if (%unpacked_package_names) {
		myinternaldie("packages stay unconfigured: " . join(' ', keys %unpacked_package_names)); 
	}

	return @result;
}

sub _get_changesets {
	my ($self, $action_graph, $ref_pending_downloads, $archives_space_limit) = @_;
	my $debug = $self->_config->var('debug::worker');

	my $sub_get_download_amount = sub {
		my ($ref_download_pack) = @_;

		return (sum map { $_->{'size'} } @$ref_download_pack) // 0;
	};

	my $ok = 0;
	my @max_download_amounts;
	my @changesets;
	for (1..$self->_config->var('cupt::worker::archives-space-limit::tries')) {
		my @action_group_list = $action_graph->topological_sort_of_strongly_connected_components();
		@action_group_list = __move_unpacks_to_configures(\@action_group_list, $action_graph);
		@action_group_list = __split_heterogeneous_actions(@action_group_list);
		@changesets = $self->_split_action_group_list_into_changesets(
				\@action_group_list, $ref_pending_downloads);
		if (not defined $archives_space_limit) {
			$ok = 1;
			last;
		}

		my @download_amounts;
		foreach my $ref_download_pack (map { $_->{'downloads'} } @changesets) {
			push @download_amounts, $sub_get_download_amount->($ref_download_pack);
		}

		my $max_download_amount = max @download_amounts;
		push @max_download_amounts, $max_download_amount;
		if ($debug) {
			mydebug('changeset maximum download amount: %s',
					human_readable_size_string($max_download_amount));
		}
		if ($max_download_amount < $archives_space_limit) {
			$ok = 1;
			last;
		}
	}

	if ($ok) {
		# optimize changesets by uniting when possible into larger parts
		my $sub_does_download_amount_fit = sub {
			my ($amount) = @_;
			if (defined $archives_space_limit) {
				return $amount < $archives_space_limit;
			} else {
				return 1;
			}
		};

		my @new_changesets;
		my $ref_current_changeset = shift @changesets;
		while (my $ref_next_changeset = shift @changesets) {
			my $current_download_amount =
					$sub_get_download_amount->($ref_current_changeset->{'downloads'});
			my $next_download_amount =
					$sub_get_download_amount->($ref_next_changeset->{'downloads'});
			if ($sub_does_download_amount_fit->($current_download_amount + $next_download_amount)) {
				# merge!
				push @{$ref_current_changeset->{'action_group_list'}},
						@{$ref_next_changeset->{'action_group_list'}};
				push @{$ref_current_changeset->{'downloads'}},
						@{$ref_next_changeset->{'downloads'}};
			} else {
				push @new_changesets, $ref_current_changeset;
				$ref_current_changeset = $ref_next_changeset;
			}
		}
		push @new_changesets, $ref_current_changeset;
		@changesets = @new_changesets;
	} else {
		# we failed to fit in limit
		mydie("unable to fit in archives space limit '%s', best try is '%s'",
				$archives_space_limit, min @max_download_amounts);
	}

	return @changesets;
}

=head2 change_system

member function, performes planned actions

Returns true if successful, false otherwise

Parameters:

I<download_progress> - reference to subclass of Cupt::Download::Progress

=cut

sub change_system ($$) {
	my ($self, $download_progress) = @_;

	my $simulate = $self->_config->var('cupt::worker::simulate');
	my $debug = $self->_config->var('debug::worker');
	my $download_only = $self->_config->var('cupt::worker::download-only');
	my $archives_space_limit = $self->_config->var('cupt::worker::archives-space-limit');
	if ($archives_space_limit !~ m/^\d+$/) {
		mydie("the option 'cupt::worker::archives-space-limit' should be numeric-only");
	}

	my $ref_actions_preview = $self->get_actions_preview();

	my @pending_downloads = $self->_prepare_downloads($ref_actions_preview, $download_progress);
	if ($download_only or not defined $archives_space_limit) {
		# download all now
		$self->_do_downloads(\@pending_downloads, $download_progress);
		@pending_downloads = ();
	};
	return 1 if $download_only;

	my $action_graph = $self->_build_actions_graph($ref_actions_preview);
	# exit when nothing to do
	defined $action_graph or return 1;

	my @changesets = $self->_get_changesets($action_graph,
			\@pending_downloads, $archives_space_limit);
	undef $action_graph;
	undef @pending_downloads;

	# doing or simulating the actions
	my $dpkg_binary = $self->_config->var('dir::bin::dpkg');
	foreach my $option ($self->_config->var('dpkg::options')) {
		$dpkg_binary .= " $option";
	}

	my $defer_triggers = $self->_config->var('cupt::worker::defer-triggers');

	$self->_do_dpkg_pre_actions();

	my $archives_directory = $self->_get_archives_directory();
	foreach my $ref_changeset (@changesets) {
		if ($debug) {
			mydebug('started changeset');
		}
		# usually, all downloads are done before any install actions
		# however, if 'cupt::worker::archives-space-limit' is turned on
		# this is no longer the case, and we will do downloads/installs by portions
		# ("changesets")
		$self->_do_downloads($ref_changeset->{'downloads'}, $download_progress);

		$self->_do_dpkg_pre_packages_actions($ref_changeset->{'action_group_list'});

		foreach my $ref_action_group (@{$ref_changeset->{'action_group_list'}}) {
			# all the actions will have the same action name by algorithm
			my $action_name = $ref_action_group->[0]->{'action_name'};

			if ($action_name eq 'remove' && $self->_config->var('cupt::worker::purge')) {
				$action_name = 'purge';
			}

			do { # do auto status info manipulations
				my %packages_changed = map { $_->{'version'}->package_name => 1 } @$ref_action_group;
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

			if ($action_name eq 'purge-config-files') {
				$action_name = 'purge';
			}

			do { # dpkg actions
				my $dpkg_command = "$dpkg_binary --$action_name";
				$dpkg_command .= ' --no-triggers' if $defer_triggers;
				# add necessary options if requested
				$dpkg_command .= $ref_action_group->[0]->{'dpkg_flags'} // '';

				foreach my $ref_action (@$ref_action_group) {
					my $action_expression;
					if ($action_name eq 'install' || $action_name eq 'unpack') {
						my $version = $ref_action->{'version'};
						$action_expression = $archives_directory . '/' .  __get_archive_basename($version);
					} else {
						my $package_name = $ref_action->{'version'}->package_name;
						$action_expression = $package_name;
					}
					$dpkg_command .= " $action_expression";
				}
				if ($debug) {
					my @stringified_versions;
					my $dpkg_flags = $ref_action_group->[0]->{'dpkg_flags'} // '';
					foreach my $ref_action (@$ref_action_group) {
						my $version = $ref_action->{'version'};
						push @stringified_versions, $version->package_name . '_' . $version->version_string;
					}
					mydebug("$action_name $dpkg_flags " . join(' ', @stringified_versions));
				}
				$self->_run_external_command($dpkg_command);
			};
		}
		if ($defer_triggers) {
			# triggers were not processed during actions perfomed before, do it now at once
			my $command = "$dpkg_binary --triggers-only --pending";
			$self->_run_dpkg_command('triggers', $command, $command);
		}

		if (defined $archives_space_limit) {
			$self->_clean_downloads($ref_changeset->{'downloads'});
		}
		if ($debug) {
			mydebug('ended changeset');
		}
	}

	$self->_do_dpkg_post_actions();

	return 1;
}

sub __generate_hash_sums ($$) {
	my ($ref_hash_sums, $path) = @_;

	my %digests = ('md5sum' => 'MD5', 'sha1sum' => 'SHA-1', 'sha256sum' => 'SHA-256');

	open(FILE, '<', $path) or
			mydie("unable to open file '%s': %s", $path, $!);
	binmode(FILE) or
			mydie("unable to set binary mode on file '%s': %s", $path, $!);

	foreach my $hash_key (keys %digests) {
		my $digest = $digests{$hash_key};
		my $hasher = Digest->new($digest);
		seek(FILE, 0, SEEK_SET) or
				mydie("unable to seek on file '%s': %s", $path, $!);
		$hasher->addfile(*FILE);
		$ref_hash_sums->{$hash_key} = $hasher->hexdigest();
	}

	close(FILE) or
			mydie("unable to close file '%s': %s", $path, $!);
	return;
}

=head2 update_release_and_index_data

member function, performes update of APT/Cupt indexes

Returns true if successful, false otherwise

Parameters:

I<download_progress> - reference to subclass of Cupt::Download::Progress

=cut

sub update_release_and_index_data ($$) {
	my ($self, $download_progress) = @_;

	my $sub_get_download_filename = sub {
		my ($target_filename) = @_;
		return dirname($target_filename) . $_download_partial_suffix .
				'/' . basename($target_filename);
	};

	my $sub_get_download_filename_basename = sub {
		my ($download_uri) = @_;
		(my $download_filename_basename = $download_uri) =~ s{(?:.*)/(.*)}{$1};
		return $download_filename_basename;
	};

	my $sub_get_download_filename_extension = sub {
		my ($download_uri) = @_;

		my $download_filename_basename = $sub_get_download_filename_basename->($download_uri);

		if ($download_filename_basename =~ m/(\.\w+)/) {
			return $1;
		} else {
			return '';
		}
	};

	my $sub_generate_moving_sub = sub {
		my ($download_path, $target_path) = @_;
		return sub {
			move($download_path, $target_path) or
					return sprintf __('%s: unable to move target file: %s'), $download_path, $!;
			return ''; # success
		};
	};

	my $sub_generate_uncompressing_sub = sub {
		my ($download_uri, $download_path, $local_path) = @_;

		my $download_filename_extension = $sub_get_download_filename_extension->($download_uri);

		# checking and preparing unpackers
		if ($download_filename_extension =~ m/^\.(lzma|bz2|gz)$/) {
			my %extension_to_uncompressor_name = ('.lzma' => 'unlzma', '.bz2' => 'bunzip2', '.gz' => 'gunzip');
			my $uncompressor_name = $extension_to_uncompressor_name{$download_filename_extension};

			if (system("which $uncompressor_name >/dev/null")) {
				mywarn("'%s' uncompressor is not available, not downloading '%s'",
						$uncompressor_name, $download_uri);
				return undef;
			}

			return sub {
				my $uncompressing_result = system("$uncompressor_name $download_path -c > $local_path");
				# anyway, remove the compressed file
				unlink $download_path; ## no critic (RequireCheckedSyscalls)
				if ($uncompressing_result) {
					return sprintf "failed to uncompress '%s', '%s' returned error %s",
							$download_path, $uncompressor_name, $uncompressing_result;
				}
				return '';
			};
		} elsif ($download_filename_extension eq '') {
			# no extension
			return $sub_generate_moving_sub->($download_path => $local_path);
		} else {
			mywarn("unknown file extension '%s', not downloading '%s'",
						$download_filename_extension, $download_uri);
			return undef;
		}
	};

	my $indexes_directory = $self->_get_indexes_directory();
	my $simulate = $self->_config->var('cupt::worker::simulate');

	my $lock;
	if (!$simulate) {
		sysopen($lock, $indexes_directory . '/lock', O_WRONLY | O_CREAT, O_EXCL) or
				mydie('unable to open indexes lock file: %s', $!);
	}

	my $cache = $self->_cache;
	my @index_entries = @{$cache->get_index_entries()};

	# run pre-actions
	foreach my $command ($self->_config->var('apt::update::pre-invoke')) {
		$self->_run_dpkg_command('pre', $command, $command);
	}

	unless ($simulate) { # unconditional clearing of partial chunks of Release[.gpg] files
		my $partial_indexes_directory = $indexes_directory . $_download_partial_suffix;
		my @paths = glob("$partial_indexes_directory/*Release*");
		unlink $_ foreach @paths; ## no critic (RequireCheckedSyscalls)

		# also create directory if it doesn't exist
		if (! -e $partial_indexes_directory) {
			mkdir($partial_indexes_directory) or
					mydie("unable to create partial directory '%s'", $partial_indexes_directory);
		}
	}

	my $master_exit_code = 0;
	do { # download manager involved part
		my $download_manager = Cupt::Download::Manager->new($self->_config, $download_progress);

		my @pids;
		foreach my $index_entry (@index_entries) {
			my $pid = fork() //
					mydie('unable to fork: %s', $!);

			if ($pid) {
				# master process
				push @pids, $pid;
			} else {
				# child
				my $exit_code = 1; # bad by default

				# wrap errors here
				eval {
					my $release_local_path = $cache->get_path_of_release_list($index_entry);

					# we'll check hash sums of local file before and after to
					# determine do we need to clean partial indexes
					#
					# this release sums won't match for sure
					my $ref_release_hash_sums = { 'md5sum' => '1', 'sha1sum' => 2, 'sha256sum' => 3 };
					if (-r $release_local_path) {
						# if the Release file already present
						__generate_hash_sums($ref_release_hash_sums, $release_local_path);
					}

					do {
						# phase 1: downloading Release file
						my $release_alias = join(' ', $index_entry->{'distribution'}, 'Release');

						my $local_path = $release_local_path;
						my $download_uri = $cache->get_download_uri_of_release_list($index_entry);
						my $download_filename = $sub_get_download_filename->($local_path);

						my $download_result = $download_manager->download(
								{
									'uri-entries' => [ {
										'uri' => $download_uri,
										'short-alias' => $release_alias,
										'long-alias' => "$index_entry->{'uri'} $release_alias",
									} ],
									'filename' => $download_filename,
									'post-action' => $sub_generate_moving_sub->($download_filename => $local_path),
								}
						);
						if ($download_result) {
							# failed to download
							mywarn("failed to download '%s'", $release_alias);
							goto CHILD_EXIT;
						}

						# phase 1.1: downloading signature for Release file
						my $signature_download_uri = "$download_uri.gpg";
						my $signature_local_path = "$local_path.gpg";
						my $signature_download_filename = "$download_filename.gpg";

						my $release_signature_alias = "$release_alias.gpg";

						my $sub_post_action = $sub_generate_moving_sub->(
								$signature_download_filename => $signature_local_path);

						my $config = $self->_config;
						if (not $simulate and not $config->var('cupt::update::keep-bad-signatures')) {
							# if we have to check signature prior to moving to canonical place
							# (for compatibility with APT tools) and signature check failed,
							# delete the downloaded file
							my $old_sub_post_action = $sub_post_action;
							$sub_post_action = sub {
								my $move_error = $old_sub_post_action->();
								return $move_error if $move_error;

								if (!Cupt::Cache::verify_signature($config, $local_path)) {
									unlink $signature_local_path or
											mywarn("unable to delete file '%s': %s", $signature_local_path, $!);
									mywarn("signature verification for '%s' failed", $release_alias);
								}
								return '';
							};
						}

						$download_result = $download_manager->download(
								{
									'uri-entries' => [ {
										'uri' => $signature_download_uri,
										'short-alias' => $release_signature_alias,
										'long-alias' => "$index_entry->{'uri'} $release_signature_alias",
									} ],
									'filename' => $signature_download_filename,
									'post-action' => $sub_post_action,
								}
						);
						if ($download_result) {
							# failed to download
							mywarn("failed to download '%s'", $release_signature_alias);
						}
					};

					do { # phase 2: downloading Packages/Sources
						my $local_path = $cache->get_path_of_index_list($index_entry);
						my $ref_download_entries = $cache->get_download_entries_of_index_list(
								$index_entry, $release_local_path);
						# checking maybe there is no difference between the local and the remote?
						if (not $simulate) {
							foreach (values %$ref_download_entries) {
								if (-e $local_path && Cupt::Cache::verify_hash_sums($_, $local_path)) {
									# yeah, really
									$exit_code = 0;
									goto CHILD_EXIT;
								}
							}
						}

						my $base_download_filename = $sub_get_download_filename->($local_path);

						# try to download files of less size first
						my @download_uris_in_order;
						do {
							my $sub_get_uri_priority = sub {
								my ($uri) = @_;
								my $extension = $sub_get_download_filename_extension->($uri);
								if ($extension eq '') {
									$extension = 'uncompressed';
								}
								$extension =~ s/^\.//; # remove starting '.' if exist
								my $result = $self->_config->var("cupt::update::compression-types::${extension}::priority");
								return $result;
							};
							@download_uris_in_order = sort {
								$sub_get_uri_priority->($b) <=> $sub_get_uri_priority->($a) or
								$ref_download_entries->{$a}->{'size'} <=> $ref_download_entries->{$b}->{'size'}
							} keys %$ref_download_entries;
						};

						my $download_result;
						foreach my $download_uri (@download_uris_in_order) {
							my $download_filename_basename = $sub_get_download_filename_basename->($download_uri);
							my $download_filename_extension = $sub_get_download_filename_extension->($download_uri);

							my $download_filename = $base_download_filename . $download_filename_extension;

							my $sub_main_post_action = $sub_generate_uncompressing_sub->(
									$download_uri, $download_filename, $local_path);
							next if not defined $sub_main_post_action;

							my $index_alias = sprintf '%s/%s %s', $index_entry->{'distribution'},
									$index_entry->{'component'}, $download_filename_basename;

							if (not $simulate) {
								# here we check for outdated dangling indexes in partial directory
								if (!Cupt::Cache::verify_hash_sums($ref_release_hash_sums, $release_local_path)) {
									# Release file has been changed during phase #1
									# delete it if present
									unlink $download_filename; ## no critic (RequireCheckedSyscalls)
								}
							}

							$download_result = $download_manager->download(
									{
										'uri-entries' => [ {
											'uri' => $download_uri,
											'short-alias' => $index_alias,
											'long-alias' => "$index_entry->{'uri'} $index_alias",
										} ],
										'filename' => $download_filename,
										'size' => $ref_download_entries->{$download_uri}->{'size'},
										'post-action' => sub {
											Cupt::Cache::verify_hash_sums($ref_download_entries->{$download_uri}, $download_filename) or
													do {
														unlink $download_filename; ## no critic (RequireCheckedSyscalls)
														return sprintf __('%s: hash sums mismatch'), $download_filename;
													};
											return $sub_main_post_action->();
										}
									}
							);
							# if all processed smoothly, exit loop
							if (!$download_result) {
								# all's good
								$exit_code = 0;
								last;
							}
						}

						if ($download_result) {
							# we could be here if neither download URI succeeded
							mywarn("failed to download index for '%s/%s'",
									$index_entry->{'distribution'}, $index_entry->{'component'});
							goto CHILD_EXIT;
						}
					};

					do {
						# phase 3 (optional): downloading file containing localized descriptions

						my $ref_download_entries = $cache->get_download_entries_of_localized_descriptions($index_entry);
						foreach my $ref_download_entry (@$ref_download_entries) {
							my ($download_uri, $local_path) = @$ref_download_entry;

							my $download_filename = $sub_get_download_filename->($local_path);

							my $download_filename_basename = $sub_get_download_filename_basename->($download_uri);
							my $download_filename_extension = $sub_get_download_filename_extension->($download_uri);

							my $sub_post_action = $sub_generate_uncompressing_sub->(
									$download_uri, $download_filename, $local_path);
							next if not defined $sub_post_action;

							my $index_alias = sprintf '%s/%s %s', $index_entry->{'distribution'},
									$index_entry->{'component'}, $download_filename_basename;

							my $download_result = $download_manager->download(
									{
										'uri-entries' => [ {
											'uri' => $download_uri,
											'short-alias' => $index_alias,
											'long-alias' => "$index_entry->{'uri'} $index_alias",
										} ],
										'filename' => $download_filename,
										'post-action' => $sub_post_action,
									}
							);
							last if not $download_result; # if all's ok
						}
					};

				};

				if ($@ and not mycatch()) {
					# Perl error
					die $@;
				}

				CHILD_EXIT:
				POSIX::_exit($exit_code);
			}
		}
		foreach my $pid (@pids) {
			waitpid $pid, 0; ## no critic (RequireCheckedSyscalls)
			# if something went bad in child, the parent won't return non-zero code too
			$master_exit_code += $?;
		}
	};

	if (!$simulate) {
		close($lock) or
				mydie('unable to close indexes lock file: %s', $!);
	}

	# run post-actions
	foreach my $command ($self->_config->var('apt::update::post-invoke')) {
		$self->_run_dpkg_command('post', $command, $command);
	}
	if ($master_exit_code == 0) {
		foreach my $command ($self->_config->var('apt::update::post-invoke-success')) {
			$self->_run_dpkg_command('post', $command, $command);
		}
	}

	return !$master_exit_code;
}

=head2 clean_archives

method, cleans archives directory.

Parameters:

I<sub_callback> - reference to callback subroutine which will be called for
each deletion with one argument - file path

I<leave_available> - whether to leave .debs that are available from package
indexes or not
=cut

sub clean_archives ($$) {
	my ($self, $sub_callback, $leave_available) = @_;

	my $archives_directory = $self->_get_archives_directory();
	my %white_list;
	if ($leave_available) {
		my $cache = $self->_cache;
		my @packages = map { $cache->get_binary_package($_) } $cache->get_binary_package_names();
		foreach my $package (@packages) {
			foreach my $version (@{$package->get_versions()}) {
				my $path = $archives_directory . '/' . __get_archive_basename($version);
				$white_list{$path} = 1;

				# checking for symlinks
				if (-l $path) {
					my $target_path = readlink $path;
					if (-e $target_path) {
						# don't delete this file too
						$white_list{$target_path} = 1;
					}
				}
			}
		}
	}

	my @paths_to_delete = glob("$archives_directory/*.deb");

	my $simulate = $self->_config->var('cupt::worker::simulate');
	if ($simulate) {
		mysimulate('deletions:');
	}
	foreach my $path (@paths_to_delete) {
		not exists $white_list{$path} or next;
		$sub_callback->($path);
		if (!$simulate) {
			unlink $path or mydie("unable to delete file '%s'", $path);
		}
	}
	return;
}

=head2 save_system_snapshot

method, saves system snapthot under specified name

Parameters:

I<snapshots> - L<Cupt::System::Snapshots|Cupt::System::Snapshots>

I<name> - name of the snapshot

=cut

sub save_system_snapshot {
	require Cwd;

	my ($self, $snapshots, $name) = @_;

	if ($name =~ m/^\./) {
		mydie("the system snapshot name cannot start with a '.'");
	}

	if (any { $_ eq $name } $snapshots->get_snapshot_names()) {
		mydie("the system snapshot named '%s' already exists", $name);
	}

	# ensuring needed tools is available
	if (system('which dpkg-repack >/dev/null 2>/dev/null')) {
		mydie("the 'dpkg-repack' binary is not available, install the package 'dpkg-repack'");
	}
	if (system('which dpkg-scanpackages >/dev/null 2>/dev/null')) {
		mydie("'dpkg-scanpackages' binary is not available, install the package 'dpkg-dev'");
	}

	my $ref_installed_versions = $self->_cache->get_system_state()->export_installed_versions();
	my @installed_package_names = map { $_->package_name } @$ref_installed_versions;

	my $snapshots_directory = $snapshots->get_snapshots_directory();
	my $snapshot_directory = $snapshots->get_snapshot_directory($name);
	my $temporary_snapshot_directory = $snapshots->get_snapshot_directory(".partial-$name");

	my $simulate = $self->_config->var('cupt::worker::simulate');

	unless ($simulate) { # creating snapshot directory
		if (! (-e $snapshots_directory and -d _)) {
			mkdir($snapshots_directory) or
					mydie("unable to create snapshots directory '%s': %s", $snapshots_directory, $!);
		}
		mkdir($temporary_snapshot_directory) or
				mydie("unable to create snapshot directory '%s': %s", $snapshot_directory, $!);
	}

	eval {
		my $sub_create_file = sub {
			my ($file, $content) = @_;

			if ($simulate) {
				mysimulate("writing file '%s'", $file);
			} else {
				open(my $fd, '>', $file) or
						mydie("unable to open '%s' for writing: %s", $file, $!);
				(print { $fd } $content) or
						mydie("unable to write to '%s': %s", $file, $!);
				close($fd) or
						mydie("unable to close '%s': %s", $file, $!);
			}
		};

		# saving list of package names
		$sub_create_file->("$temporary_snapshot_directory/installed_package_names",
				join("\n", @installed_package_names) . "\n");

		# building source line
		$sub_create_file->("$temporary_snapshot_directory/source",
				"deb file:$snapshots_directory $name/\n");

		do { # backing up the installed packages
			my $current_directory = getcwd();

			unless ($simulate) {
				# this is for dfsg-repack
				chdir($temporary_snapshot_directory) or
						mydie("unable to set current directory to '%s': %s", $temporary_snapshot_directory, $!);
			}

			foreach my $package_name (sort @installed_package_names) {
				my $version = $self->_cache->get_binary_package($package_name)->get_installed_version();
				my $architecture = $version->architecture;

				$self->_run_external_command("dpkg-repack --arch=$architecture $package_name");

				# dpkg-repack uses dpkg-deb -b, which produces file in format
				#
				# <package_name>_<stripped_version_string>_<arch>.deb
				#
				# I can't say why the hell someone decided to strip version here,
				# so I have to rename the file properly.
				unless ($simulate) {
					# finding a file
					my @files = glob("${package_name}_*.deb");
					if (scalar @files != 1) {
						mydie("dpkg-repack produced either no or more than one Debian archive for the package '%s'",
								$package_name);
					}
					my $bad_filename = $files[0];
					my $good_filename = sprintf '%s_%s_%s.deb', $package_name,
							$version->version_string, $architecture;

					move($bad_filename, $good_filename) or
							mydie("unable to move '%s' to '%s': %s", $bad_filename, $good_filename, $!);
				}
			}

			(my $local_path_base = $snapshot_directory) =~ tr{/}{_};
			my $packages_file_name = "${local_path_base}_Packages";

			# building a release file for them
			$self->_run_external_command("dpkg-scanpackages . > $packages_file_name");

			unless ($simulate) {
				do { # create Release file
					my $release_content;
					# I don't use heredoc because of indenting.
					$release_content .= "Origin: Cupt\n";
					$release_content .= "Label: snapshot\n";
					$release_content .= "Suite: snapshot\n";
					$release_content .= "Codename: snapshot\n";

					my $previous_lc_time = setlocale(LC_TIME, 'C');
					$release_content .= 'Date: ' . strftime('%a, %d %b %Y %H:%M:%S UTC', gmtime()) . "\n";
					setlocale(LC_TIME, $previous_lc_time);

					$release_content .= 'Architectures: all ' . $self->_config->var('apt::architecture') . "\n";
					$release_content .= "Description: Cupt-made system snapshot\n";

					my $ref_hash_sums = { 'md5sum' => '1', 'sha1sum' => '2', 'sha256sum' => '3' };
					__generate_hash_sums($ref_hash_sums, "./$packages_file_name");

					my $size = File::stat::stat("./$packages_file_name")->size();
					defined $size or
							myinternaldie('undefined size for Packages');

					$release_content .= "MD5Sum:\n " . $ref_hash_sums->{'md5sum'} . " $size Packages\n";
					$release_content .= "SHA1:\n " . $ref_hash_sums->{'sha1sum'} . " $size Packages\n";
					$release_content .= "SHA256:\n " . $ref_hash_sums->{'sha256sum'} . " $size Packages\n";

					my $file = "$temporary_snapshot_directory/${local_path_base}_Release";
					open(my $fd, '>', $file) or
							mydie("unable to open '%s' for writing: %s", $file, $!);
					print { $fd } $release_content or
							mydie("unable to write to '%s': %s", $file, $!);
					close($fd) or
							mydie("unable to close '%s': %s", $file, $!);
				};

				chdir($current_directory) or
						mydie("unable to set current directory to '%s': %s", $current_directory, $!);
			}
		};

		unless ($simulate) {
			# all done, do final move
			move($temporary_snapshot_directory, $snapshot_directory) or
					mydie("unable to move directory '%s' to '%s': %s",
							$temporary_snapshot_directory, $snapshot_directory, $!);
		}
	};

	if (mycatch()) {
		# deleting partially constructed snapshot (try)
		chdir('/') or
				mywarn("unable to set current directory to '/': %s", $!);
		eval {
			$self->_run_external_command("rm -r $temporary_snapshot_directory");
		};
		if (mycatch()) {
			mywarn("unable to delete partial snapshot directory '%s'", $temporary_snapshot_directory);
		}

		myerr("error constructing system snapshot named '%s'", $name);
		myredie();
	}

	return;
}

=head2 remove_system_snapshot

method, removes the system snapshot, identified by name

Parameters:

I<snapshots> - L<Cupt::System::Snapshots|Cupt::System::Snapshots>

I<name> - a name of the snapshot

=cut

sub remove_system_snapshot {
	my ($self, $snapshots, $name) = @_;

	if (none { $_ eq $name } $snapshots->get_snapshot_names()) {
		mydie("unable to find snapshot named '%s'", $name);
	}

	my $snapshot_directory = $snapshots->get_snapshot_directory($name);

	$self->_run_external_command("rm -r $snapshot_directory");

	return;
}

=head2 rename_system_snapshot

method, renames the system snapshot

Parameters:

I<snapshots> - L<Cupt::System::Snapshots|Cupt::System::Snapshots>

I<previous_name> - previous name of the snapshot

I<new_name> - new name of the snapshot

=cut

sub rename_system_snapshot {
	my ($self, $snapshots, $previous_name, $new_name) = @_;

	if (none { $_ eq $previous_name } $snapshots->get_snapshot_names()) {
		mydie("unable to find snapshot named '%s'", $previous_name);
	}
	if (any { $_ eq $new_name } $snapshots->get_snapshot_names()) {
		mydie("the system snapshot named '%s' already exists", $new_name);
	}

	my $previous_snapshot_directory = $snapshots->get_snapshot_directory($previous_name);
	my $new_snapshot_directory = $snapshots->get_snapshot_directory($new_name);

	$self->_run_external_command("mv $previous_snapshot_directory $new_snapshot_directory");

	return;
}

1;

