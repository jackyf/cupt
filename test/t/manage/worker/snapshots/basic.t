use TestCupt;
use Test::More tests => 3;

use strict;
use warnings;

my $cupt = TestCupt::setup();

eval get_inc_code('common');

test_snapshot_list('', 'there are no snapshots');
test_snapshot_command('unkn', qr/^E: unsupported action 'unkn'$/m, 'unknown snapshot subcommand');
test_snapshot_command(' ', qr/^E: the action is not specified$/m, 'no subcommand');

