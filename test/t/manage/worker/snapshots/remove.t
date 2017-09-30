use Test::More tests => 6;

my $cupt = setup();

eval get_inc_code('common');

test_snapshot_command($cupt, "remove", qr/^E: no snapshot name specified$/m, 'no arguments');
test_snapshot_command($cupt, "remove xyz", qr/^E: unable to find a snapshot named 'xyz'$/m, 'snapshot does not exist');

save_snapshot($cupt, 'qpr');
save_snapshot($cupt, 'mnk');

test_snapshot_command($cupt, "remove mnk", undef, "snapshot existed");
test_snapshot_list($cupt, "qpr\n", "removal succeeded");

