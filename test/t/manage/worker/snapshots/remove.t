use TestCupt;
use Test::More tests => 6;

use strict;
use warnings;

my $cupt = TestCupt::setup();

eval get_inc_code('common');

test_snapshot_command("remove", qr/^E: no snapshot name specified$/m, 'no arguments');
test_snapshot_command("remove xyz", qr/^E: unable to find a snapshot named 'xyz'$/m, 'snapshot does not exist');

save_snapshot('qpr');
save_snapshot('mnk');

test_snapshot_command("remove mnk", undef, "snapshot existed");
test_snapshot_list("qpr\n", "removal succeeded");

