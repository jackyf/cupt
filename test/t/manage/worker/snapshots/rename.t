use TestCupt;
use Test::More tests => 6;

use strict;
use warnings;

my $cupt = TestCupt::setup();

eval get_inc_code('common');

test_snapshot_command("rename", qr/^E: no previous snapshot name specified$/m, 'no arguments supplied');
test_snapshot_command("rename tyu", qr/^E: no new snapshot name specified$/m, 'one argument supplied');
test_snapshot_command("rename tyu xyz", qr/^E: unable to find a snapshot named 'tyu'$/m, 'source snapshot does not exist');

save_snapshot('qpr');
test_snapshot_command("rename qpr xyz", undef, 'source snapshot exists');
test_snapshot_list("xyz\n", 'renaming succeeded');

