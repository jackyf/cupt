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
}

sub _parse_dpkg_status {
	# Status lines are similar to apt Packages ones, with two differences:
	# 1) 'Status' field: "<want> <flag> <status>"
	#    a) 'want' - "wanted" status of package
	#       can be: 'unknown', 'install', 'hold', 'deinstall', 'purge'
	   flag = ok, reinstreq, hold, hold-reinstreq
	   status = not-installed, unpacked, half-configured,
				half-installed, config-files, post-inst-failed, 
				removal-failed, installed
}

