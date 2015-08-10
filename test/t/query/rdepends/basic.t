use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $packages =
		entail(compose_package_record('abc', '1')) .
		entail(compose_package_record('abc', '2') . "Depends: xyz\n") .
		entail(compose_package_record('xyz', '3'));

my $cupt = TestCupt::setup('packages' => $packages);

my $output = stdout("$cupt rdepends xyz");

unlike($output, qr/abc 1/, "abc 1 doesn't depend on xyz");
like($output, qr/abc 2/, "abc 2 does depend on xyz");

