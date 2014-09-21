use TestCupt;
use Test::More tests => 1;

my $cupt = TestCupt::setup('dpkg_status' => compose_installed_record('abc', '1'));

like(get_first_offer("$cupt remove abc --no-remove"), regex_offer(), "--no-remove doesn't prevent user-requested removals");

