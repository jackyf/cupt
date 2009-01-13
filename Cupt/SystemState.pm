package Cupt::SystemState;
# TODO: implement parsing /var/lib/dpkg/status

use 5.10.0;
use strict;
use warnings;

use Cupt::Core;
use Cupt::Cache::Pkg;
use Cupt::Cache::BinaryVersion;

use fields qw(config cache);

sub new {
	my $class = shift;
	my $self = fields::new($class);

	$self->{config} = shift;
	$self->{cache} = shift;

	# in next line we don't use 'dir' and 'dir::state' variables as we do
	# in all others path builder functions, that's apt decision
	my $dpkg_status_path = $self->{config}->var('dir::state::status');
	$self->_parse_dpkg_status();
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

	open(STATUS, '<', $file) or mydie("unable to open file %s: %s'", $file, $!);
	close(STATUS) or mydie("unable to close file %s: %s", $file, $!);
}

