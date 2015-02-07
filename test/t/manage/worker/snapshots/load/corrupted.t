use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $cupt;

eval get_inc_code('../common');


my $snapshot_name = '201501';
my $snapshot_path = "var/lib/cupt/snapshots/$snapshot_name";

sub setup_cupt {
	$cupt = TestCupt::setup(
		'dpkg_status' =>
			entail(compose_installed_record('ooo', 1)) .
			entail(compose_installed_record('ppp', 2)) ,
	);
}

sub test {
	my ($corrupter, $description) = @_;

	setup_cupt();

	subtest $description => sub {
		save_snapshot($snapshot_name);
		$corrupter->();
		test_snapshot_command("load $snapshot_name", qr/^E:.* $description$/m, 'error message printed');
	}
}

test(
	sub { system("echo qqq >> $snapshot_path/installed_package_names"); },
	"the package 'qqq' doesn't exist"
);

test(
	sub { system("echo 2 > $snapshot_path/format"); },
	"unsupported snapshot format '2'"
);

