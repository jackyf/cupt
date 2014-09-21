use TestCupt;
use Test::More tests => 3;

use strict;
use warnings;

my $packages = <<END;
Package: main
Version: 1
Architecture: all
Depends: dep1 | dep2 | dep3
SHA1: 1

Package: dep1
Version: 1
Architecture: all
SHA1: 2

Package: dep2
Version: 3
Architecture: all
SHA1: 3

Package: dep3
Version: 1
Architecture: all
SHA1: 4

END

my $cupt = TestCupt::setup('packages' => $packages);

my $output = `yes 'N' | $cupt -s install main 2>&1`;

like($output, regex_offer(), "resolving succeeded");
like($output, qr/dep1.*dep2/s, "dep1 is offered before dep2");
like($output, qr/dep2.*dep3/s, "dep2 is offered before dep3");

