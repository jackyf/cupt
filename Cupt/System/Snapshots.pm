#***************************************************************************
#*   Copyright (C) 2009 by Eugene V. Lyubimkin                             *
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
package Cupt::System::Snapshots;

=head1 NAME

Cupt::System::Snapshots - hold info about available system snapshots

=cut

use warnings;
use strict;

use Cupt::LValueFields qw(_config);

use File::Basename;
use List::MoreUtils 0.23 qw(apply none);

use Cupt::Core;

=head1 METHODS

=head2 new

creates L<Cupt::System::Snapshots|Cupt::System::Snapshots> object

Parameters:

I<config> - L<Cupt::Config|Cupt::Config>

=cut

sub new {
	my ($class, $config) = @_;
	my $self = bless [] => $class;
	$self->_config = $config;
	return $self;
}

=head2 get_snapshots_directory

method, returns directory path which contains all available snapshots

=cut

sub get_snapshots_directory {
	my ($self) = @_;
	return $self->_config->var('dir') .
			$self->_config->var('cupt::directory::state') . '/' .
			$self->_config->var('cupt::directory::state::snapshots');
}

=head2 get_snapshot_directory

method, returns directory which contains named snapshot

Parameters:

I<name> - name of the snapshot

=cut

sub get_snapshot_directory {
	my ($self, $snapshot_name) = @_;
	return $self->get_snapshots_directory() . "/$snapshot_name";
}

=head2 get_snapshot_names

method, returns list of names of available snapshots

=cut

sub get_snapshot_names {
	my ($self) = @_;
	my $snapshots_directory = $self->get_snapshots_directory();
	return map { basename($_) } glob("$snapshots_directory/*");
}

=head2 setup_config_for_snapshot

method, modifies config to load only snapshot versions of packages

Parameters:

I<name> - name of the snapshot

=cut

sub setup_config_for_snapshot {
	my ($self, $snapshot_name) = @_;

	my $config = $self->_config;

	my $new_base_dir = $self->get_snapshot_directory($snapshot_name);
	$new_base_dir =~ s{^/}{};

	$config->set_regular_var('dir::state' => $new_base_dir);
	$config->set_regular_var('dir::state::lists' => '.');
	$config->set_regular_var('dir::cache' => $new_base_dir);
	$config->set_regular_var('dir::cache::archives' => '.');

	foreach my $sub_option (qw(main parts preferences preferencesparts)) {
		$config->set_regular_var("dir::etc::$sub_option" => $config->var('dir::etc') . '/' .
				$config->var("dir::etc::$sub_option"));
	}
	$config->set_regular_var('dir::etc' => $new_base_dir);
	$config->set_regular_var('dir::etc::sourcelist' => 'source');
	$config->set_regular_var('dir::etc::sourceparts' => 'non-existent');

	return;
}

=head2 setup_config_for_snapshot

method, schedules snapshot versions of packages to resolver

Parameters:

I<name> - name of the snapshot

I<resolver> - L<Cupt::System::Resolver|Cupt::System::Resolver>

=cut

sub setup_resolver_for_snapshot {
	my ($self, $snapshot_name, $resolver) = @_;

	if (none { $snapshot_name eq $_ } $self->get_snapshot_names()) {
		mydie("there is no system snapshot named '%s'", $snapshot_name);
	}

	my $snapshot_directory = $self->get_snapshot_directory($snapshot_name);
	my $cache = $resolver->cache();

	my @all_package_names = $cache->get_binary_package_names();
	
	my $snapshot_packages_file = "$snapshot_directory/installed_package_names";
	open(my $fd, '<', $snapshot_packages_file) or
			mydie("unable to open '%s': %s", $snapshot_packages_file, $!);
	(my @snapshot_package_names = <$fd>) or
			mydie("unable to read '%s': %s", $snapshot_packages_file, $!);
	chomp($_) foreach @snapshot_package_names;
	close($fd) or
			mydie("unable to close '%s': %s", $snapshot_packages_file, $!);
	
	my %scheduled_package_names;
	foreach my $snapshot_package_name (@snapshot_package_names) {
		my $package = $cache->get_binary_package($snapshot_package_name);
		defined $package or
				mydie("the package '%s' doesn't exist", $snapshot_package_name);
		my $found = 0;
		VERSION:
		foreach my $version (@{$package->get_versions()}) {
			my @available_as = @{$version->available_as};
			foreach (@available_as) {
				if ($_->{release}->{archive} eq 'snapshot') {
					$resolver->install_version($version);
					$found = 1;
					last VERSION;
				}
			}
		}
		$found or mydie("unable to find snapshot version for the package '%s'", $snapshot_package_name);
		$scheduled_package_names{$snapshot_package_name} = 1;
	}

	foreach my $package_name (@all_package_names) {
		next if $scheduled_package_names{$package_name};
		$resolver->remove_package($package_name);
	}

	return undef;
}

1;
