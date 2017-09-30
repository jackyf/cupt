use Test::More tests => 4;

my $cupt = TestCupt::setup(
	'dpkg_status' => [ compose_installed_record('aa', 1) ]
);

eval get_inc_code('common');


save_snapshot($cupt, 'xyz');
test_snapshot_list($cupt, "xyz\n", "listing one snapshot");

save_snapshot($cupt, 'klm');
test_snapshot_list($cupt, "klm\nxyz\n", "listing two snapshots");

