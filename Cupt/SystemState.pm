package Cupt::SystemState;
# TODO: implement parsing /var/lib/dpkg/status

use 5.10.0;
use strict;
use warnings;

use Cupt::Core;
use Cupt::Cache::Pkg;
use Cupt::Cache::BinaryVersion;

use fields qw(config cache installed_info);

sub new {
	my $class = shift;
	my $self = fields::new($class);

	$self->{config} = shift;
	$self->{cache} = shift;

	# in next line we don't use 'dir' and 'dir::state' variables as we do
	# in all others path builder functions, that's apt decision
	my $dpkg_status_path = $self->{config}->var('dir::state::status');
	if (! -r $dpkg_status_path) {
		mydie("unable to open dpkg status file '%s'", $dpkg_status_path);
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
	#               removal-failed, installed
	# 2) purged packages contain only 'Package', 'Status', 'Priority'
	#    and 'Section' fields.
	# TODO: get info about 'post-inst-failed' and 'removal-failed' statuses.

	my ($self, $file) = @_;

	my $fh;
	open($fh, '<', $file) or mydie("unable to open file %s: %s'", $file, $!);
	open(PACKAGES, "/bin/grep -b '^Package: ' $file |"); 
	open(STATUSES, "/bin/grep '^Status: ' $file |"); 
	open(VERSIONS, "/bin/grep '^Version: ' $file |"); 

	eval {
		while (<PACKAGES>) {
			chomp;

			# firstly, make sure that this is 'Package' line
			# "12345:" is prefix by grep
			m/^(\d+):Package: (.*)/ or
					mydie("expected 'Package' line, but haven't got it, got '%s' instead", $_);

			# don't check package name for correctness, dpkg has to check this already
			my $package_name = $2;

			my $offset = $1 + length("Package: $package_name\n");

			# try to read status line
			$_ = readline(STATUSES);
			defined($_) or
					mydie("expected 'Status' line, but haven't got it (for package '%s')", $package_name);

			chomp;

			# extract info from 'Status' line, primary correctness was already checked by grep
			m/^Status: (.*)/;

			my %installed_info;
			($installed_info{'want'}, $installed_info{'flag'}, $installed_info{'status'}) =
					split / /, $1 or
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
				if ($_ ne 'not-installed' and $_ ne 'unpacked' and
					$_ ne 'half-configured' and $_ ne 'half-installed' and
					$_ ne 'config-files' and $_ ne 'post-inst-failed' and
					$_ ne 'removal-failed' and $_ ne 'installed')
				{
					mydie("malformed 'status' status indicator (for package '%s')", $package_name);
				}
			};

			# extract info from 'Status' line, primary correctness was already checked by grep
			m/^Version: (.*)/;

			my $version_string = $1;

			$version_string =~ m/^$version_string_regex$/ or
					mydie("bad version '%s'", $version_string);

			$installed_info{'version'} = $version_string;

			# add parsed info to installed_info
			push @{$self->{installed_info}}, \%installed_info;

			if ($installed_info{'flag'} eq 'ok' and $installed_info{'status'} eq 'installed') {
				# this conditions mean that package is properly installed
				# and have full entry info, so add it (info) to cache

				# adding new version to cache
				$self->{cache}->{binary_packages}->{$package_name} //= Cupt::Cache::Pkg->new();

				Cupt::Cache::Pkg::add_entry(
						$self->{cache}->{binary_packages}->{$package_name}, 'Cupt::Cache::BinaryVersion',
						$package_name, $fh, $offset, undef, \%Cupt::Cache::_empty_release_info);
			}
		}
	};
	if (mycatch()) {
		myerr("error parsing system status file '%s'", $file);
		myredie();
	}

	close(PACKAGES) or mydie("unable to close grep pipe");
	close(STATUSES) or mydie("unable to close grep pipe");
	close(VERSIONS) or mydie("unable to close grep pipe");
}

1;

