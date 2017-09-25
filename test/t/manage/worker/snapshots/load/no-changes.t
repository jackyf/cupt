use TestCupt;
use Test::More tests => 3;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('abc', 3)),
);

eval get_inc_code('../common');


save_snapshot($cupt, 'sn1');

my $offer = get_first_offer("$cupt snapshot load sn1");
like($offer, regex_offer(), 'load succeeded');

is_deeply(get_offered_versions($offer), {}, 'no changes proposed');

