use TestCupt;
use Test::More tests => 9;

use strict;
use warnings;

my $cupt = TestCupt::setup();

eval get_inc_code('../common');


test_snapshot_command($cupt, 'save', qr/^E: no snapshot name/m, 'no arguments');

test_snapshot_command($cupt, 'save kkk', undef, 'saving first snapshot');
test_snapshot_command($cupt, 'save lll', undef, 'saving second snapshot');

test_snapshot_command($cupt, 'save lll', qr/^E: the system snapshot named 'lll' already exists$/m, 'second snapshot name already exists');
test_snapshot_command($cupt, 'remove lll', undef, 'removing second snapshot');
test_snapshot_command($cupt, 'save lll', undef, 'snapshot name is again free for saving after removal');

test_snapshot_command($cupt, 'save kkk', qr/^E: the system snapshot named 'kkk' already exists$/m, 'first snapshot name already exists');
test_snapshot_command($cupt, 'rename kkk mmm', undef, 'renaming first snapshot');
test_snapshot_command($cupt, 'save kkk', undef, 'snapshot name is again free for saving after rename');

