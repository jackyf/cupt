use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $dpkg_status = compose_installed_record('abc', '1');

my $cupt = TestCupt::setup('dpkg_status' => $dpkg_status);

is(exitcode("$cupt show abc"), 0, "there is 'abc' binary package");
isnt(exitcode("$cupt show abd"), 0, "there is no 'abd' binary package");
