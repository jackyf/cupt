use Test::More tests => 3;

require(get_rinclude_path('common'));

my $cupt = setup();

test_snapshot_list($cupt, '', 'there are no snapshots');
test_snapshot_command($cupt, 'unkn', qr/^E: unsupported action 'unkn'$/m, 'unknown snapshot subcommand');
test_snapshot_command($cupt, ' ', qr/^E: the action is not specified$/m, 'no subcommand');

