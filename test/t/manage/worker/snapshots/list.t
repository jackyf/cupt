use Test::More tests => 4;

require(get_rinclude_path('common'));

my $cupt = TestCupt::setup(
	'dpkg_status' => [ compose_installed_record('aa', 1) ]
);

save_snapshot($cupt, 'xyz');
test_snapshot_list($cupt, "xyz\n", "listing one snapshot");

save_snapshot($cupt, 'klm');
test_snapshot_list($cupt, "klm\nxyz\n", "listing two snapshots");

