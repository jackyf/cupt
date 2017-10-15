use TestCupt;
use Test::More tests => 1;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('ip', '3.2')),
	'packages' =>
		entail(compose_package_record('ip', '3.1')) .
		entail(compose_package_record('ip', '3.2')) .
		entail(compose_package_record('ip', '3.2.2')) .
		entail(compose_package_record('ip', '3.3.4')) .
		entail(compose_package_record('ip', '3.2')) .
		entail(compose_package_record('ip', '3.5')) .
		entail(compose_package_record('ip', '4.0')),
);

my $offers = get_all_offers("$cupt safe-upgrade");
my $expected_offer_count = 5; # 4 upgrade versions + 1 unsatisfied upgrade
is(get_offer_count($offers), $expected_offer_count) or diag($offers);

