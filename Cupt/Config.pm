package Cupt::Config;

use strict;
use warnings;

use Cupt::ISCConfigParser;
use Cupt::Core;

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
			'acquire::http::dl-limit' => 0,
			'apt::acquire::max-default-age::debian-security' => 7,
			'apt::authentication::trustcdrom' => 0,
			'apt::cache::allversions' => 0,
			'apt::cache::important' => 0,
			'apt::cache::namesonly' => 0,
			'apt::cache::recursedepends' => 0,
			'apt::default-release' => undef,
			'apt::install-recommends' => 1,
			'apt::install-suggests' => 0,
			'dir' => '/',
			'dir::bin::dpkg' => '/usr/bin/dpkg',
			'dir::cache' => 'home/jackyf/Work/Programming/linux/cupt',
			'dir::cache::archives' => 'temp/archives',
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
			'dpkg::tools::options::/usr/bin/apt-listchanges::version' => 2,

			'cupt::downloader::max-simultaneous-downloads' => 4,
			'cupt::resolver::keep-recommends' => 1,
			'cupt::resolver::keep-suggests' => 0,
			'cupt::resolver::auto-remove' => 1,
			'cupt::worker::purge' => 0,
			'debug::resolver' => 0,
		},

		list_vars => {
			'apt::neverautoremove' => [],
			'apt::update::pre-invoke' => [],
			'apt::update::post-invoke' => [],
			'dpkg::pre-install-pkgs' => [],
		},

	};
	$self->{regular_vars}->{'apt::architecture'} = __get_architecture();
	bless $self, $class;
	$self->_read_configs();
	return $self;
}

sub var {
	my $self = shift;
	my $var_name = shift;
	if (exists ($self->{regular_vars}->{$var_name})) {
		return $self->{regular_vars}->{$var_name};
	} elsif (defined ($self->{list_vars}->{$var_name})) {
		return $self->{list_vars}->{$var_name};
	} else {
		mydie("attempt to get wrong option %s", $var_name);
	}
}

sub set_regular_var {
	my $self = shift;
	my $var_name = lc(shift);
	if (exists ($self->{regular_vars}->{$var_name})) {
		my $new_value = shift;
		$self->{regular_vars}->{$var_name} = $new_value;
	} else {
		mydie("attempt to set wrong option %s", $var_name);
	}
}

sub set_list_var {
	my $self = shift;
	my $var_name = lc(shift);
	if (defined ($self->{list_vars}->{$var_name})) {
		my $new_value = shift;
		push @{$self->{list_vars}->{$var_name}}, $new_value;
	} else {
		mydie("attempt to set wrong option %s", $var_name);
	}
}

sub _read_configs {
	my $self = shift;
	my $parser = new Cupt::ISCConfigParser;

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
	push @config_files, "$root_prefix$etc_dir/$main_file";

	foreach (@config_files) {
		$parser->parse_file($_);
	}
}

sub __get_architecture {
	my $answer = qx/dpkg --print-architecture/;
	chomp($answer);
	return $answer;
}

1;

