use TestCupt;
use Test::More tests => 4;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('aa', 1)) ,
);

eval get_inc_code('common');


save_snapshot('xyz');
test_snapshot_list("xyz\n", "listing one snapshot");

save_snapshot('klm');
test_snapshot_list("klm\nxyz\n", "listing two snapshots");

