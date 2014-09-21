use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $packages = <<END;
Package: abc
Version: 1
Architecture: all
Conflicts: abc
SHA1: 1

Package: def
Version: 1
Architecture: all
Conflicts: abc
SHA1: 2

END

my $cupt = TestCupt::setup('packages' => $packages);

like(get_first_offer("$cupt install abc"), regex_offer(), "package doesn't conflict with itself");
like(get_first_offer("$cupt install abc def"), regex_no_solutions(), "conflicting packages cannot be installed together");

