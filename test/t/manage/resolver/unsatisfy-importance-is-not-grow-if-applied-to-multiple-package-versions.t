use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $packages = <<END;
Package: abc
Version: 1
Architecture: all
Provides: vp
SHA1: 1

Package: abc
Version: 2
Architecture: all
Provides: vp
SHA1: 1

END

my $cupt = TestCupt::setup('packages' => $packages);

my $output = get_first_offer("$cupt install --importance=3000 abc --importance=2500 --satisfy vp-");

like($output, regex_offer(), "resolving succeeded");
is(get_offered_version($output, 'abc'), '2', "abc is offered");
