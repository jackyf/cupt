use TestCupt;
use Test::More tests => 3;

use strict;
use warnings;

my $packages =
		entail(compose_package_record('xyz', '3')) .
		entail(compose_package_record('abc', '1') . "Source: abc-source\n") .
		entail(compose_package_record('abc', '2.1') . "Source: abc-source (4.5)\n");

my $cupt = TestCupt::setup('packages' => $packages);

like(stdout("$cupt show xyz"), qr/^Source: xyz\n/m, "source package name is binary package name if omitted");
like(stdout("$cupt show abc=1"), qr/^Source: abc-source\n/m, "source version is binary version if omitted");
like(stdout("$cupt show abc=2.1"), qr/^Source: abc-source\nSource version: 4.5\n/m, "source version is parsed if present");

