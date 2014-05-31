use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $dpkg_status =
		entail(compose_installed_record('abc', '1')) .
		entail(compose_installed_record('abc', '2') . "Depends: xyz\n") .
		entail(compose_installed_record('xyz', '3'));

my $cupt = TestCupt::setup('dpkg_status' => $dpkg_status);

my $output = stdout("$cupt rdepends xyz");

unlike($output, qr/abc 1/, "abc 1 doesn't depend on xyz");
like($output, qr/abc 2/, "abc 2 does depend on xyz");

