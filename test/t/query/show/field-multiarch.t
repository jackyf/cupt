use TestCupt;
use Test::More tests => 1;

use strict;
use warnings;

my $packages = entail(compose_package_record('abc', '1') . "Multi-Arch: allowed\n");

my $cupt = TestCupt::setup('packages' => $packages);

like(stdout("$cupt show abc"), qr/^Multi-Arch: allowed$/m, "'Multi-Arch' field is shown");
