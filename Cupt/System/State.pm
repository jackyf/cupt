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
package Cupt::System::State;

=head1 NAME

Cupt::System::State - holds info about installed packages

=cut

use 5.10.0;
use strict;
use warnings;

use Cupt::Core;
use Cupt::Cache::Package;
use Cupt::Cache::BinaryVersion;

use fields qw(_config _cache _installed_info);

=head1 METHODS

=head2 new

creates new Cupt::System::State object, usually shouldn't be called by hand

Parameters:

I<config> - reference to L<Cupt::Config|Cupt::Config>

I<cache> - reference to L<Cupt::Cache|Cupt::Cache>

=cut

sub new {
	my $class = shift;
	my $self = fields::new($class);

	$self->{_config} = shift;
	$self->{_cache} = shift;

	# in next line we don't use 'dir' and 'dir::state' variables as we do
	# in all others path builder functions, that's apt decision
	my $dpkg_status_path = $self->{_config}->var('dir::state::status');
	if (! -r $dpkg_status_path) {
		mydie("unable to open dpkg status file '%s': %s", $dpkg_status_path, $!);
	}
	$self->_parse_dpkg_status($dpkg_status_path);
	return $self;
}

sub _parse_dpkg_status {
	# Status lines are similar to apt Packages ones, with two differences:
	# 1) 'Status' field: "<want> <flag> <status>"
	#    a) <want> - desired status of package
	#       can be: 'unknown', 'install', 'hold', 'deinstall', 'purge'
	#    b) <flag> - ok/bad
	#       can be: ok, reinstreq, hold, hold-reinstreq
	#    c) <status> - current status of package
	#       can be: not-installed, unpacked, half-configured,
	#               half-installed, config-files, post-inst-failed, 
	#               removal-failed, installed, triggers-pending,
	#               triggers-awaited
	# 2) purged packages contain only 'Package', 'Status', 'Priority'
	#    and 'Section' fields.

	my ($self, $file) = @_;

	# filling release info
	my %release_info = %Cupt::Cache::_empty_release_info; ## no critic (PrivateVars)
	$release_info{archive} = 'installed';
	$release_info{codename} = 'now';
	$release_info{label} = '';
	$release_info{version} = '';
	$release_info{vendor} = 'dpkg';
	$release_info{component} = '';
	$release_info{base_uri} = '';
	$release_info{signed} = 0;

	push @{$self->{_cache}->_release_data->{binary}}, \%release_info;

	my $fh;
	open($fh, '<', $file) or mydie("unable to open file '%s': %s", $file, $!);

	eval {
		local $/ = "\n\n";
		while (<$fh>) {
			m'^Package: (.*?)$.*?^Status: (.*?)$.*?^Version:'sm or next;

			my %installed_info;

			# don't check package name for correctness, dpkg has to check this already
			my $package_name = $1;

			($installed_info{'want'}, $installed_info{'flag'}, $installed_info{'status'}) =
					split / /, $2 or
					mydie("malformed 'Status' line (for package '%s')", $package_name);

			do { # check 'want'
				local $_ = $installed_info{'want'};
				if ($_ ne 'install' and $_ ne 'deinstall' and $_ ne 'purge' and
					$_ ne 'hold' and $_ ne 'unknown')
				{
					mydie("malformed 'desired' status indicator (for package '%s')", $package_name);
				}
			};
			do { # check 'flag'
				local $_ = $installed_info{'flag'};
				if ($_ ne 'ok' and $_ ne 'reinstreq' and
					$_ ne 'hold' and $_ ne 'hold-reinstreq')
				{
					mydie("malformed 'error' status indicator (for package '%s')", $package_name);
				}
			};
			do { # check 'status'
				local $_ = $installed_info{'status'};
				if (m/^trigger/) {
					mydie("some dpkg triggers are not processed, please run 'dpkg --triggers-only -a' as root");
				}
				if ($_ ne 'not-installed' and $_ ne 'unpacked' and
					$_ ne 'half-configured' and $_ ne 'half-installed' and
					$_ ne 'config-files' and $_ ne 'post-inst-failed' and
					$_ ne 'removal-failed' and $_ ne 'installed')
				{
					mydie("malformed 'status' status indicator (for package '%s')", $package_name);
				}
			};

			if ($installed_info{'flag'} eq 'ok')
			{
				if ($installed_info{'status'} ne 'not-installed' and $installed_info{'status'} ne 'config-files') {
					# this conditions mean that package is installed or
					# semi-installed, regardless it has full entry info, so
					# add it (info) to cache

					my $offset = tell($fh) - length($_);

					push @{$self->{_cache}->_binary_packages->{$package_name}},
							[ 'Cupt::Cache::BinaryVersion', $package_name, $fh, $offset, \%release_info ];

					if (m/^Provides: (.*?)$/m) {
						$self->{_cache}->_process_provides_subline($package_name, $1);
					}
				}

				# add parsed info to installed_info
				$self->{_installed_info}->{$package_name} = \%installed_info;
			}
		}
	};
	if (mycatch()) {
		myerr("error parsing system status file '%s'", $file);
		myredie();
	}

	return;
}

sub _get_installed_version_for_package_name {
	my ($self, $package_name) = @_;

	my $package = $self->{_cache}->get_binary_package($package_name);
	return undef if not defined $package;

	my $installed_version = $package->get_installed_version();
	return $installed_version;
}

=head2 get_status_for_version

method, return undef if the version isn't installed, installed info otherwise
(see L</get_installed_info>)

Parameters:

I<version> - reference to L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion>

=cut

sub get_status_for_version {
	my ($self, $version) = @_;
	my $package_name = $version->package_name;
	my $ref_info = $self->{_installed_info}->{$package_name};
	if (defined $ref_info ) {
		my $installed_version = $self->_get_installed_version_for_package_name($package_name);
		if (defined $installed_version and $installed_version->version_string eq $version->version_string) {
			return $self->{_installed_info}->{$package_name};
		} else {
			return undef;
		}
	} else {
		return undef;
	}
}

=head2 get_installed_info

returns installed info of the package

Parameters:

I<package_name> - package name to lookup

Returns:
  {
    'want' => I<want>,
    'flag' => I<flag>,
    'status' => I<status>,
    'version_string' => I<version_string>,
  }

if info is present, undef otherwise

=cut

sub get_installed_info ($$) {
	my ($self, $package_name) = @_;
	if (exists $self->{_installed_info}->{$package_name}) {
		my %info = %{$self->{_installed_info}->{$package_name}};
		my $installed_version = $self->_get_installed_version_for_package_name($package_name);
		$info{'version_string'} = defined $installed_version ?
				$installed_version->version_string : undef;
		return \%info;
	} else {
		return undef;
	}
}

=head2 get_installed_version_string

Parameters:

I<package_name> - package name to lookup

Returns:

version string of installed version or undef if no version of the
package is installed

=cut

sub get_installed_version_string ($$) {
	my ($self, $package_name) = @_;
	my $ref_installed_info = $self->get_installed_info($package_name);
	return undef if not defined $ref_installed_info;

	if ($ref_installed_info->{'status'} eq 'installed') {
		return $ref_installed_info->{'version_string'};
	} else {
		return undef;
	}
}

=head2 export_installed_versions

method, returns array reference of installed I<version>s (for those packages
that have configured version in the system)

where:

I<version> - reference to L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion>

=cut

sub export_installed_versions ($) {
	my ($self) = @_;
	my @result;

	while (my ($package_name, $ref_installed_info) = each %{$self->{_installed_info}}) {
		my $status = $ref_installed_info->{'status'};
		if ($status eq 'not-installed' or $status eq 'config-files') {
			next;
		}
		my $package = $self->{_cache}->get_binary_package($package_name);
		my $version = $package->get_installed_version();
		defined $version or
				mydie("the package '%s' does not have installed version", $package_name);
		push @result, $version;
		next;
	}
	return \@result;
}

1;

