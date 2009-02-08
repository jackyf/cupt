package Cupt::System::State;

use 5.10.0;
use strict;
use warnings;

use Cupt::Core;
use Cupt::Cache::Pkg;
use Cupt::Cache::BinaryVersion;

=head1 FIELDS

=head2 installed_info

contains info of packages which dpkg knows about

Format:
  {
    I<package_name> => {
      'want' => I<want>,
      'flag' => I<flag>,
      'status' => I<status>,
    }
  }

=cut

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

	# fake base_uri, defines local package
	my $base_uri = "";

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

			if ($installed_info{'flag'} eq 'ok' and
				($installed_info{'status'} eq 'installed' or $installed_info{'status'} eq 'config-files'))
			{
				if ($installed_info{'status'} eq 'installed') {
					# this conditions mean that package is properly installed
					# and have full entry info, so add it (info) to cache

					# adding new version to cache
					$self->{cache}->{binary_packages}->{$package_name} //= Cupt::Cache::Pkg->new();

					Cupt::Cache::Pkg::add_entry(
							$self->{cache}->{binary_packages}->{$package_name}, 'Cupt::Cache::BinaryVersion',
							$package_name, $fh, $offset, \$base_uri, \%Cupt::Cache::_empty_release_info);

				}

				# try to read version line
				$_ = readline(VERSIONS);
				defined($_) or
						mydie("expected 'Version' line, but haven't got it (for package '%s')", $package_name);

				chomp;

				# extract info from 'Version' line, primary correctness was already checked by grep
				m/^Version: (.*)/;

				my $version_string = $1;

				$version_string =~ m/^$version_string_regex$/ or
						mydie("bad version '%s'", $version_string);

				$installed_info{'version'} = $version_string;

				# add parsed info to installed_info
				$self->{installed_info}->{$package_name} = \%installed_info;
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

	# additionally, preparse Provides fields for status file
	$self->{cache}->_process_provides_in_index_file($file);
}

sub get_status_for_version {
	my ($self, $version) = @_;
	my $package_name = $version->{package_name};
	if (exists $self->{installed_info}->{$package_name}) {
		my $ref_info = $self->{installed_info}->{$package_name};
		if ($ref_info->{'version'} eq $version->{version}) {
			return $ref_info;
		}
	}
	return undef;
}

sub get_installed_version_string {
	my ($self, $package_name) = @_;
	if (exists $self->{installed_info}->{$package_name}) {
		my $ref_info = $self->{installed_info}->{$package_name};
		return $ref_info->{'version'};
	}
	return undef;
}

sub export_installed_versions ($) {
	my ($self) = @_;
	my @result;

	while (my ($package_name, $ref_installed_info) = each %{$self->{installed_info}}) {
		$ref_installed_info->{'status'} eq 'installed' or next;
		my $version_string = $ref_installed_info->{'version'};
		my $package = $self->{cache}->get_binary_package($package_name);
		my $version = $self->{cache}->get_specific_version($package, $version_string);
		defined $version or
				mydie("cannot find version '%s' for package '%s'", $version_string, $package_name);
		push @result, $version;
		next;
	}
	return \@result;
}

1;

