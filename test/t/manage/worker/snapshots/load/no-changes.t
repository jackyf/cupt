use Test::More tests => 3;

my $cupt = setup(
	'dpkg_status' => [ compose_installed_record('abc', 3) ]
);

eval get_inc_code('../common');


save_snapshot($cupt, 'sn1');

my $offer = get_first_offer("$cupt snapshot load sn1");
like($offer, regex_offer(), 'load succeeded');

is_deeply(get_offered_versions($offer), {}, 'no changes proposed');

