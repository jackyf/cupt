use TestCupt;
use Test::More tests => 6;

use strict;
use warnings;

my $packages =
		entail(compose_package_record('abc', '1')) .
		entail(compose_package_record('abc', '2') . "Multi-Arch: allowed\n");

my $cupt = TestCupt::setup('packages' => $packages);

my $parse_failure = qr/^\QE: failed to parse\E/m;

like(get_first_offer("$cupt satisfy 'abc:zzz'"), regex_no_solutions(), "unknown ':zzz'");
like(get_first_offer("$cupt satisfy 'abc:zzz (>= 1)'"), regex_no_solutions(), "unknown ':zzz' with version");
like(get_first_offer("$cupt satisfy 'abc:y'"), regex_no_solutions(), "unknown ':y'");
like(get_first_offer("$cupt satisfy 'abc:aaaaaaaaaaaaaaaaaaaaaaannnnnnnnnnnnnnnnnnnnnnn13245mmmmmmmmm___ajsdf'"), regex_no_solutions(), "unknown ':<garbage>'");
like(get_first_offer("$cupt satisfy 'abc:'"), $parse_failure, "invalid ':'");
like(get_first_offer("$cupt satisfy 'abc: (<< 3)'"), $parse_failure, "invalid ':' with version");

