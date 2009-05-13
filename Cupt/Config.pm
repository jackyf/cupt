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

use Cupt::Config::ISCConfigParser;
use Cupt::Core;

=head1 METHODS

=head2 new

creates a new Cupt::Config object

=cut

sub new {
	my $class = shift;
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
	my $self = {
		regular_vars => {
			'acquire::http::timeout' => 120,
			'acquire::https::timeout' => 120,
			'acquire::ftp::timeout' => 120,
			'acquire::file::timeout' => 20,
			'apt::acquire::max-default-age::debian-security' => 7,
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
			'dir::state' => 'var/lib/apt',
			'dir::state::extendedstates' => 'extended_states',
			'dir::state::lists' => 'lists',
			'dir::state::status' => '/var/lib/dpkg/status',
			'gpgv::trustedkeyring' => '/var/lib/cupt/trusted.gpg',

			'acquire::http::allow-redirects' => 1,
			'cupt::downloader::max-simultaneous-downloads' => 2,
			'cupt::resolver::auto-remove' => 1,
			'cupt::resolver::external-command' => undef,
			'cupt::resolver::keep-recommends' => 1,
			'cupt::resolver::keep-suggests' => 0,
			'cupt::resolver::max-solution-count' => 256,
			'cupt::resolver::no-remove' => 0,
			'cupt::resolver::type' => 'multiline-fair',
			'cupt::worker::defer-triggers' => 1,
			'cupt::worker::download-only' => 0,
			'cupt::worker::purge' => 0,
			'cupt::worker::simulate' => 0,
			'debug::resolver' => 0,
			'debug::worker' => 0,
		},

		_optional_patterns => [
			'acquire::*::*::proxy',
			'acquire::*::proxy',
			'acquire::*::*::dl-limit',
			'acquire::*::dl-limit',
			'acquire::*::*::timeout',
			'acquire::*::timeout',
			'dpkg::tools::options::*',
			'dpkg::tools::options::*::*',
		],

		list_vars => {
			'apt::neverautoremove' => [],
			'apt::update::pre-invoke' => [],
			'apt::update::post-invoke' => [],
			'dpkg::pre-install-pkgs' => [],
			'dpkg::pre-invoke' => [],
			'dpkg::post-invoke' => [],
			'rpm::pre-invoke' => [],
			'rpm::post-invoke' => [],
		},

	};
	bless $self, $class;
	$self->set_regular_var('apt::architecture', $self->_get_architecture());
	$self->_read_configs();
	return $self;
}

# determines if the option matches some of the optional patterns
sub _is_optional_option ($$) {
	my ($self, $var_name) = @_;
	foreach my $pattern (@{$self->{_optional_patterns}}) {
		(my $regex = $pattern) =~ s/\*/[^:]*?/g;
		return 1 if ($var_name =~ m/$regex/);
	}
	return 0;
}

=head2 var

method, returns value of config option - scalar in case of scalar option and
list in case of list option

Parameters:

I<option_name> - string name of option

=cut

sub var {
	my $self = shift;
	my $var_name = shift;
	if (exists ($self->{regular_vars}->{$var_name})) {
		return $self->{regular_vars}->{$var_name};
	} elsif (defined ($self->{list_vars}->{$var_name})) {
		return @{$self->{list_vars}->{$var_name}};
	} elsif ($self->_is_optional_option($var_name)) {
		return undef;
	} else {
		mydie("attempt to get wrong option '%s'", $var_name);
	}
}

=head2 set_regular_var

method, sets scalar option I<option_name> to I<option_value>

Parameters:

I<option_name> - string option name to set

I<option_value> - desired value

Returns: true on success, false on fail.

=cut

sub set_regular_var {
	my $self = shift;
	my $var_name = lc(shift);
	if (exists $self->{regular_vars}->{$var_name} || $self->_is_optional_option($var_name)) {
		my $new_value = shift;
		$self->{regular_vars}->{$var_name} = $new_value;
		return 1;
	} else {
		mywarn("attempt to set wrong option '%s'", $var_name);
		return 0;
	}
}

=head2 set_list_var

method, adds a I<option_value> to a list option I<option_name>

Parameters:

I<option_name> - string option name to advance

I<option_value> - value to add

Returns: true on success, false on fail.

=cut

sub set_list_var {
	my $self = shift;
	my $var_name = lc(shift);
	if (defined ($self->{list_vars}->{$var_name})) {
		my $new_value = shift;
		push @{$self->{list_vars}->{$var_name}}, $new_value;
	} else {
		mywarn("attempt to set wrong option '%s'", $var_name);
	}
}

sub _read_configs {
	my $self = shift;
	my $parser = new Cupt::Config::ISCConfigParser;

	my $regular_option_sub = sub {
		my $option_name = shift;
		my $value = shift;
		$value =~ s/"(.*)"/$1/;
		$self->set_regular_var($option_name, $value);
	};

	my $list_option_sub = sub {
		my $option_name = shift;
		map { s/"(.*)"/$1/; $self->set_list_var($option_name, $_); } @_;
	};

	$parser->set_regular_handler($regular_option_sub);
	$parser->set_list_handler($list_option_sub);

	my $root_prefix = $self->var('dir');
	my $etc_dir = $self->var('dir::etc');

	my $parts_dir = $self->var('dir::etc::parts');
	my @config_files = glob("$root_prefix$etc_dir/$parts_dir/*");

	my $main_file = $self->var('dir::etc::main');
	my $main_file_path = "$root_prefix$etc_dir/$main_file";
	push @config_files, $main_file_path if -e $main_file_path;

	foreach (@config_files) {
		eval {
			$parser->parse_file($_);
		};
		if (mycatch()) {
			mywarn("skipped configuration file '%s'", $_);
		}
	}
}

sub _get_architecture ($) {
	my ($self) = @_;
	my $dpkg_binary = $self->var('dir::bin::dpkg');
	my $answer = qx/$dpkg_binary --print-architecture/;
	chomp($answer);
	return $answer;
}

1;

