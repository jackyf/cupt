use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $packages =
		entail(compose_package_record('abc', '1') . "Depends: defAxyz\n") .
		entail(compose_package_record('abc', '2') . "Depends: klm35!#0er2");

my $cupt = TestCupt::setup('packages' => $packages);

isnt(exitcode("$cupt show abc=1"), 0, "big letters not allowed");
isnt(exitcode("$cupt show abc=2"), 0, "garbage symbols not allowed");

