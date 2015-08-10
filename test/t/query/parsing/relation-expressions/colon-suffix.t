use TestCupt;
use Test::More tests => 4;

use strict;
use warnings;

my $packages =
		entail(compose_package_record('abc', '1') . "Depends: def:xyz\n") .
		entail(compose_package_record('abc', '2') . "Depends: klm:aaa (>= 1.2.3)\n");

my $cupt = TestCupt::setup('packages' => $packages);

my $abc1output = stdout("$cupt depends abc=1");
like($abc1output, qr/Depends: def.*\n/, "colon is allowed (versionless dependency)");
like($abc1output, qr/Depends: def:xyz\n/, "colon is parsed (versionless dependency)");

my $abc2output = stdout("$cupt depends abc=2");
like($abc2output, qr/Depends: klm.* \(>= 1.2.3\)\n/, "colon is allowed (versioned dependency)");
like($abc2output, qr/Depends: klm:aaa \(>= 1.2.3\)\n/, "colon is parsed (versioned dependency)");

