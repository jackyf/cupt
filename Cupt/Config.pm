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
package Cupt::Config;

=head1 NAME

Cupt::Config - store and retrieve APT-style config variables

=cut

use strict;
use warnings;

use Storable;

use Cupt::LValueFields qw(_regular_vars _list_vars _regular_compatibility_vars _optional_patterns);
use Cupt::Config::ISCConfigParser;
use Cupt::Core;

=head1 METHODS

=head2 new

creates a new Cupt::Config object

=cut

sub new {
	my $class = shift;
	my $self = bless [] => $class;
	# APT::Build-Essential "";
	# APT::Build-Essential:: "build-essential";
	# APT::Acquire "";
	# APT::Acquire::Translation "environment";
	# Dir::State::cdroms "cdroms.list";
	# Dir::State::userstatus "status.user";
	# Dir::Etc::vendorlist "vendors.list";
	# Dir::Etc::vendorparts "vendors.list.d";
	# Dir::Bin::methods "/usr/lib/apt/methods";
	# Dir::Log "var/log/apt";
	# Dir::Log::Terminal "term.log";
	#
	$self->_regular_vars = {
		# used APT vars
		'acquire::http::timeout' => 120,
		'acquire::https::timeout' => 120,
		'acquire::ftp::timeout' => 120,
		'acquire::file::timeout' => 20,
		'acquire::retries' => 0,
		'apt::acquire::max-default-age::debian-security' => 7,
		'apt::acquire::translation' => 'environment',
		'apt::architecture' => undef, # will be set a bit later
		'apt::authentication::trustcdrom' => 0,
		'apt::cache::allversions' => 0,
		'apt::cache::important' => 0,
		'apt::cache::namesonly' => 0,
		'apt::cache::recursedepends' => 0,
		'apt::default-release' => undef,
		'apt::install-recommends' => 1,
		'apt::install-suggests' => 0,
		'apt::get::allowunauthenticated' => 0,
		'dir' => '/',
		'dir::bin::dpkg' => '/usr/bin/dpkg',
		'dir::cache' => 'var/cache/apt',
		'dir::cache::archives' => 'archives',
		'dir::etc' => 'etc/apt',
		'dir::etc::sourcelist' => 'sources.list',
		'dir::etc::sourceparts' => 'sources.list.d',
		'dir::etc::parts' => 'apt.conf.d',
		'dir::etc::main' => 'apt.conf',
		'dir::etc::preferences' => 'preferences',
		'dir::etc::preferencesparts' => 'preferences.d',
		'dir::state' => 'var/lib/apt',
		'dir::state::extendedstates' => 'extended_states',
		'dir::state::lists' => 'lists',
		'dir::state::status' => '/var/lib/dpkg/status',
		'gpgv::trustedkeyring' => '/var/lib/cupt/trusted.gpg',
		'quiet' => 0,

		# unused APT vars
		'apt::cache-limit' => undef,
		'apt::get::build-dep-automatic' => 1,
		'apt::get::show-upgraded' => 0,
		'acquire::pdiffs' => 1,

		# Cupt vars
		'acquire::http::allow-redirects' => 1,
		'cupt::cache::obey-hold' => 1000000,
		'cupt::console::allow-untrusted' => 0,
		'cupt::console::assume-yes' => 0,
		'cupt::directory::state' => 'var/lib/cupt',
		'cupt::directory::state::snapshots' => 'snapshots',
		'cupt::downloader::max-simultaneous-downloads' => 2,
		'cupt::downloader::protocols::file::priority' => 300,
		'cupt::downloader::protocols::copy::priority' => 250,
		'cupt::downloader::protocols::debdelta::priority' => 150,
		'cupt::downloader::protocols::https::priority' => 125,
		'cupt::downloader::protocols::http::priority' => 100,
		'cupt::downloader::protocols::ftp::priority' => 80,
		'cupt::downloader::protocols::file::methods::file::priority' => 100,
		'cupt::downloader::protocols::copy::methods::file::priority' => 100,
		'cupt::downloader::protocols::debdelta::methods::debdelta::priority' => 100,
		'cupt::downloader::protocols::https::methods::curl::priority' => 100,
		'cupt::downloader::protocols::http::methods::curl::priority' => 100,
		'cupt::downloader::protocols::ftp::methods::curl::priority' => 100,
		'cupt::update::compression-types::gz::priority' => 100,
		'cupt::update::compression-types::bz2::priority' => 100,
		'cupt::update::compression-types::lzma::priority' => 100,
		'cupt::update::compression-types::uncompressed::priority' => 100,
		'cupt::update::keep-bad-signatures' => 0,
		'cupt::resolver::auto-remove' => 1,
		'cupt::resolver::external-command' => undef,
		'cupt::resolver::keep-recommends' => 1,
		'cupt::resolver::keep-suggests' => 0,
		'cupt::resolver::max-solution-count' => 256,
		'cupt::resolver::no-remove' => 0,
		'cupt::resolver::quality-bar' => -50,
		'cupt::resolver::synchronize-source-versions' => 'none',
		'cupt::resolver::track-reasons' => 0,
		'cupt::resolver::type' => 'fair',
		'cupt::worker::archives-space-limit' => undef,
		'cupt::worker::archives-space-limit::tries' => 20,
		'cupt::worker::defer-triggers' => 0,
		'cupt::worker::download-only' => 0,
		'cupt::worker::purge' => 0,
		'cupt::worker::simulate' => 0,
		'debug::downloader' => 0,
		'debug::resolver' => 0,
		'debug::worker' => 0,
		'debug::gpgv' => 0,
	};

	$self->_regular_compatibility_vars = {
		'apt::get::allowunauthenticated' => 'cupt::console::allow-untrusted',
		'apt::get::assume-yes' => 'cupt::console::assume-yes',
		'apt::get::automaticremove' => 'cupt::resolver::auto-remove',
		'apt::get::purge' => 'cupt::worker::purge',
	};

	$self->_optional_patterns = [
		# used APT vars
		'acquire::*::*::proxy',
		'acquire::*::proxy::*',
		'acquire::*::proxy',
		'acquire::*::*::dl-limit',
		'acquire::*::dl-limit::*',
		'acquire::*::dl-limit',
		'acquire::*::*::timeout',
		'acquire::*::timeout::*',
		'acquire::*::timeout',
		'dpkg::tools::options::*',
		'dpkg::tools::options::*::*',

		# unused APT vars
		'acquire::compressiontypes::*',
		'apt::archives::*',
		'apt::periodic::*',
		'aptlistbugs::*',
		'unattended-upgrade::*',
		'aptitude::*',
		'dselect::*',

		# used Cupt vars
		'cupt::downloader::protocols::*::priority',
		'cupt::downloader::protocols::*::methods',
		'cupt::downloader::protocols::*::methods::*::priority',
	];

	$self->_list_vars = {
		# used APT vars
		'apt::neverautoremove' => [],
		'apt::update::pre-invoke' => [],
		'apt::update::post-invoke' => [],
		'apt::update::post-invoke-success' => [],
		'dpkg::options' => [],
		'dpkg::pre-install-pkgs' => [],
		'dpkg::pre-invoke' => [],
		'dpkg::post-invoke' => [],

		# unused APT vars
		'rpm::pre-invoke' => [],
		'rpm::post-invoke' => [],
		'apt::never-markauto-sections::*' => [],

		# Cupt vars
		'cupt::downloader::protocols::file::methods' => [ 'file' ],
		'cupt::downloader::protocols::copy::methods' => [ 'file' ],
		'cupt::downloader::protocols::debdelta::methods' => [ 'debdelta' ],
		'cupt::downloader::protocols::https::methods' => [ 'curl' ],
		'cupt::downloader::protocols::http::methods' => [ 'curl' ],
		'cupt::downloader::protocols::ftp::methods' => [ 'curl' ],
		'cupt::resolver::synchronize-source-versions::exceptions' => ['db', 'linux-\d.\d'],
		# hack to work around packages with strict unkeepable
		# Pre-Depends and ability to damage the system when
		# dependencies is not satisfied
		'cupt::worker::allow-indirect-upgrade' => [
			'libc6-i686',
			'openjdk-6-jre',
			'openjdk-6-jre-lib',
			'openjdk-6-jre-headless',
			'openoffice.org-core',
			'openoffice.org-common',
			'openoffice.org-writer2latex',
			'openoffice.org-java-common',
		],
	};

	$self->set_scalar('apt::architecture', $self->_get_architecture());
	$self->_read_configs();
	return $self;
}

=head2 clone

method, returns a copy of the object

=cut

sub clone ($) {
	return Storable::dclone($_[0]);
}

# determines if the option matches some of the optional patterns
sub _is_optional_option ($$) {
	my ($self, $var_name) = @_;
	foreach my $pattern (@{$self->_optional_patterns}) {
		(my $regex = $pattern) =~ s/\*/[^:]*?/g;
		return 1 if ($var_name =~ m/^$regex$/);
	}
	return 0;
}

=head2 get_scalar_option_names

method, returns the array of the scalar options' names

=cut

sub get_scalar_option_names {
	my ($self) = @_;

	return keys %{$self->_regular_vars};
}

=head2 get_list_option_names

method, returns the array of the list options' names

=cut

sub get_list_option_names {
	my ($self) = @_;

	return keys %{$self->_list_vars};
}

=head2 get_string

method, returns the value of the string config option, may be undef if the
option is not set

Parameters:

I<option_name> - the name of the option, string

=cut

sub get_string { ## no critic (RequireFinalReturn)
	my ($self, $option_name) = @_;

	if (exists ($self->_regular_vars->{$option_name})) {
		return $self->_regular_vars->{$option_name};
	} elsif ($self->_is_optional_option($option_name)) {
		return undef;
	} else {
		mydie("an attempt to get wrong scalar option '%s'", $option_name);
	}
	# we shouldn't ever reach it
}

=head2 get_number

method, returns the value of the number config option, may be undef if the
option is not set

Parameters:

I<option_name> - the name of the option, string

=cut

sub get_number {
	my ($self, $option_name) = @_;
	my $result = $self->get_string($option_name);
	if (defined $result) {
		if ($result !~ m/^(?:\+|-)?\d+$/) {
			mydie("the value '%s' of the number option '%s' contains non-digit characters",
					$result, $option_name);
		}
	}
	return $result;
}

=head2 get_bool

method, returns the value of the boolean config option, may be undef if the
option is not set

Parameters:

I<option_name> - the name of the option, string

=cut

sub get_bool {
	my ($self, $option_name) = @_;
	my $result = $self->get_string($option_name);
	if (defined $result) {
		if ($result eq 'false' or $result eq 'no') {
			$result = 0;
		}
	}
	return $result;
}

=head2 get_list

method, returns the array of the values of the list config option

Parameters:

I<option_name> - the name of the option, string

=cut

sub get_list { ## no critic (RequireFinalReturn)
	my ($self, $option_name) = @_;

	if (defined $self->_list_vars->{$option_name}) {
		return @{$self->_list_vars->{$option_name}};
	} elsif ($self->_is_optional_option($option_name)) {
		return ();
	} else {
		mydie("an attempt to get wrong list option '%s'", $option_name);
	}
	# we shouldn't ever reach it
}

=head2 set_scalar

method, sets scalar option I<option_name> to I<option_value>

Parameters:

I<option_name> - string option name to set

I<option_value> - desired value

Returns: true on success, false on fail.

=cut

sub set_scalar {
	my ($self, $var_name, $new_value) = @_;
	my $original_var_name = $var_name;
	$var_name = lc($var_name);

	# translation to cupt variable names
	if (exists $self->_regular_compatibility_vars->{$var_name}) {
		# setting the value for old variable
		$self->_regular_vars->{$var_name} = $new_value;

		$var_name = $self->_regular_compatibility_vars->{$var_name};
	}

	if (exists $self->_regular_vars->{$var_name} || $self->_is_optional_option($var_name)) {
		$self->_regular_vars->{$var_name} = $new_value;
		return 1;
	} else {
		mywarn("attempt to set wrong option '%s'", $original_var_name);
		return 0;
	}
}

=head2 set_list

method, adds a I<option_value> to a list option I<option_name>

Parameters:

I<option_name> - string option name to advance

I<option_value> - value to add

Returns: true on success, false on fail.

=cut

sub set_list {
	my ($self, $var_name, $new_value) = @_;
	my $original_var_name = $var_name;
	$var_name = lc($var_name);
	if (not defined $self->_list_vars->{$var_name} and $self->_is_optional_option($var_name)) {
		$self->_list_vars->{$var_name} = [];
	}
	if (defined ($self->_list_vars->{$var_name})) {
		push @{$self->_list_vars->{$var_name}}, $new_value;
	} else {
		mywarn("attempt to set wrong option '%s'", $original_var_name);
	}
	return;
}

sub _read_configs {
	my ($self) = @_;
	my $parser = Cupt::Config::ISCConfigParser->new();

	my $sub_regular_option = sub {
		my ($option_name, $value) = @_;
		$value =~ s/"(.*)"/$1/;
		$self->set_scalar($option_name, $value);
	};

	my $sub_list_option = sub {
		my ($option_name, @values) = @_;
		foreach my $value (@values) {
			$value =~ s/"(.*)"/$1/;
			$self->set_list($option_name, $value);
		}
	};

	my $sub_clear_directive = sub {
		my ($option_name_to_clear) = @_;

		foreach my $regular_option_name (keys %{$self->_regular_vars}) {
			if ($regular_option_name =~ m/^$option_name_to_clear/) {
				$self->_regular_vars->{$regular_option_name} = undef;
			}
		}
		foreach my $list_option_name (keys %{$self->_list_vars}) {
			if ($list_option_name =~ m/^$option_name_to_clear/) {
				$self->_list_vars->{$list_option_name} = [];
			}
		}
	};

	$parser->set_regular_handler($sub_regular_option);
	$parser->set_list_handler($sub_list_option);
	$parser->set_clear_handler($sub_clear_directive);

	my $root_prefix = $self->get_string('dir');
	my $etc_dir = $self->get_string('dir::etc');

	my $parts_dir = $self->get_string('dir::etc::parts');
	my @config_files = glob("$root_prefix$etc_dir/$parts_dir/*");

	my $main_file = $self->get_string('dir::etc::main');
	my $main_file_path = "$root_prefix$etc_dir/$main_file";
	if(defined $ENV{APT_CONFIG}) {
		$main_file_path = $ENV{APT_CONFIG};
	}
	push @config_files, $main_file_path if -e $main_file_path;

	foreach my $config_file (@config_files) {
		eval {
			$parser->parse_file($config_file);
		};
		if (mycatch()) {
			mywarn("skipped configuration file '%s'", $config_file);
		}
	}
	return;
}

sub _get_architecture ($) {
	my ($self) = @_;
	my $dpkg_binary = $self->get_string('dir::bin::dpkg');
	my $answer = qx/$dpkg_binary --print-architecture/;
	chomp($answer);
	return $answer;
}

1;

