use TestCupt;
use Test::More tests => 6;

use strict;
use warnings;

my $cupt = TestCupt::setup();

eval get_inc_code('common');

test_snapshot_command($cupt, "rename", qr/^E: no previous snapshot name specified$/m, 'no arguments supplied');
test_snapshot_command($cupt, "rename tyu", qr/^E: no new snapshot name specified$/m, 'one argument supplied');
test_snapshot_command($cupt, "rename tyu xyz", qr/^E: unable to find a snapshot named 'tyu'$/m, 'source snapshot does not exist');

save_snapshot($cupt, 'qpr');
test_snapshot_command($cupt, "rename qpr xyz", undef, 'source snapshot exists');
test_snapshot_list($cupt, "xyz\n", 'renaming succeeded');

