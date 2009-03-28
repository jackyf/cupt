package Cupt::System::Worker;

use 5.10.0;
use warnings;
use strict;

use Graph;
use Digest;
use Fcntl qw(:seek);
use List::Util qw(sum);
use File::Copy;

use Cupt::Core;
use Cupt::Download::Manager;

my $_download_partial_suffix = '/partial';
=head1 FIELDS

I<config> - reference to Cupt::Config

I<cache> - reference to Cupt::Cache

I<desired_state> - { I<package_name> => { 'version' => I<version> } }

=cut

use fields qw(_config _cache _system_state desired_state);

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
	$self->{_config} = shift;
	$self->{_cache} = shift;
	$self->{_system_state} = $self->{_cache}->get_system_state();
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
			mydie("unable to open file '%s'", $path);
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
			mydie("unable to close file '%s'", $path);

	return 1;
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
    'total_bytes' => I<total_bytes>,
    'need_bytes' => I<need_bytes>,
  }

where:
I<packages> = [ { 'package_name' => I<package_name>, 'version' => I<version> }... ]

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
		}
	}

	return \%result;
}

=head2 get_sizes_preview

Returns (I<total_bytes>, I<need_bytes>);

I<total_bytes> - total byte count needed for action,
I<need_bytes> - byte count, needed to download, <= I<total_bytes>

=cut

sub get_sizes_preview ($$) {
	my ($self, $ref_actions_preview) = @_;
	# size estimating of operation
	my $archives_location = $self->_get_archives_location();
	my $total_bytes = 0;
	my $need_bytes = 0;
	my @ref_package_entries = map { @{$ref_actions_preview->{$_}} } ('install', 'upgrade', 'downgrade');
	foreach my $ref_package_entry (@ref_package_entries) {
		my $version = $ref_package_entry->{version};
		my $size = $version->{size};
		$total_bytes += $size;
		$need_bytes += $size; # for start
		my $basename = __get_archive_basename($ref_package_entry->{version});
		my $path = $archives_location . '/' . $basename;
		-e $path or next; # skip if the file is not present in the cache dir
		__verify_hash_sums($version, $path) or next; # skip if the file is not what we want
		# ok, no need to download the file
		$need_bytes -= $size;
	}

	return ($total_bytes, $need_bytes);
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
			foreach my $ref_package_entry (@{$ref_actions_preview->{$user_action}}) {
				#foreach my $package_name (map { $_->{package_name} } @{$ref_actions_preview->{$user_action}}) {
				my $package_name = $ref_package_entry->{package_name};
				my $version_string;
				if ($user_action eq 'install' ||
					$user_action eq 'upgrade' ||
					$user_action eq 'downgrade')
				{
					$version_string = $ref_package_entry->{'version'}->{version_string};
				} else {
					$version_string = $self->{_system_state}->get_installed_version_string($package_name);
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
	my ($self, $ref_relation_expressions, $action_name, $ref_inner_action, $graph) = @_;

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
					my $ref_master_action = $action_name eq 'remove' ? $ref_current_action : $ref_inner_action;
					my $ref_slave_action = $action_name eq 'remove' ? $ref_inner_action : $ref_current_action;

					$graph->add_edge($ref_slave_action, $ref_master_action);

					last SATISFYING_VERSIONS;
				}
			}
		}
	}
}

=head2 do_actions

member function, performes planned actions

Returns true if successful, false otherwise

Parameters:

I<download_progress> - reference to subclass of Cupt::Download::Progress

=cut

sub do_actions ($$) {
	my ($self, $download_progress) = @_;

	my @action_groups;
	my $scg;
	# building actions graph
	do {
		if (!defined $self->{desired_state}) {
			myinternaldie("worker desired state is not given");
		}
		my $ref_actions_preview = $self->get_actions_preview();

		# action = {
		# 	'package_name' => package
		# 	'version_string' => version_string,
		# 	'action_name' => ('unpack' | 'configure' | 'remove')
		# }
		my $graph = new Graph ('directed' => 1, 'refvertexed' => 1);

		$self->_fill_actions($ref_actions_preview, $graph);

		# maybe, we have nothing to do?
		return if scalar $graph->vertices() == 0;

		# fill the actions' dependencies
		foreach my $ref_inner_action ($graph->vertices()) {
			my $package_name = $ref_inner_action->{'package_name'};
			given ($ref_inner_action->{'action_name'}) {
				when ('unpack') {
					# if the package has pre-depends, they needs to be satisfied before
					# unpack (policy 7.2)
					my $desired_version = $self->{desired_state}->{$package_name}->{version};
					# pre-depends must be unpacked before
					$self->_fill_action_dependencies(
							$desired_version->{pre_depends}, 'unpack', $ref_inner_action, $graph);
				}
				when ('configure') {
					# configure can be done only after the unpack of the same version
					my $desired_version = $self->{desired_state}->{$package_name}->{version};

					# pre-depends must be configured before
					$self->_fill_action_dependencies(
							$desired_version->{pre_depends}, 'configure', $ref_inner_action, $graph);
					# depends must be configured before
					$self->_fill_action_dependencies(
							$desired_version->{depends}, 'configure', $ref_inner_action, $graph);

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
							$system_version->{pre_depends}, 'remove', $ref_inner_action, $graph);
					# depends must be removed after
					$self->_fill_action_dependencies(
							$system_version->{depends}, 'remove', $ref_inner_action, $graph);
				}
			}
		}

		$scg = $graph->strongly_connected_graph();

		# topologically sorted actions
		@action_groups = $scg->topological_sort();
	};

	my @pending_downloads;
	do { # downloading packages
		my $archives_location = $self->_get_archives_location();
		foreach my $action_group (@action_groups) {
			my @vertices_group = @{$scg->get_vertex_attribute($action_group, 'subvertices')};
			# all the actions will have the same action name by algorithm
			my $action_name = $vertices_group[0]->{'action_name'};
			if ($action_name eq 'unpack') {
				# we have to download this package(s)
				foreach my $ref_action (@vertices_group) {
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
							mydie("no available download URIs for $package_name $version_string");

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
									return __('hash sums mismatch');
							move($download_filename, $target_filename) or
									return __('unable to move target file');
							return 0;
						},
					};
					$download_progress->set_short_alias_for_uri($uri, $package_name);
				}
			}
		}
	};

	my $simulate = $self->{_config}->var('cupt::worker::simulate');

	if ($simulate) {
		foreach (@pending_downloads) {
			print __("downloading"), ": $_\n";
		}
	} else {
		# don't bother ourselves with download preparings if nothing to download
		if (scalar @pending_downloads) {
			my @download_list;

			my $download_size = sum map { $_->{'size'} } @pending_downloads;
			$download_progress->set_total_estimated_size($download_size);

			my $download_manager = new Cupt::Download::Manager($self->{_config}, $download_progress);

			my $download_result = $download_manager->download(@pending_downloads);
			# fail and exit if it was something bad with downloading
			return 0 if $download_result;

			$download_progress->finish();
		}
	}


	foreach my $action_group (@action_groups) {
		my @vertices_group = @{$scg->get_vertex_attribute($action_group, 'subvertices')};
		# all the actions will have the same action name by algorithm
		my $action_name = $vertices_group[0]->{'action_name'};
		print __("simulating"), ": dpkg --$action_name";
		foreach my $ref_action (@vertices_group) {
			my $package_expression = $ref_action->{'package_name'};
			if ($action_name eq 'unpack') {
				$package_expression .= '<' . $ref_action->{'version_string'} . '>';
			} elsif ($action_name eq 'remove' && $self->{_config}->var('cupt::worker::purge')) {
				$action_name = 'purge';
			}
			print " $package_expression";
		}
		say "";
	}

	return 1;
}

1;

