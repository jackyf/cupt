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

