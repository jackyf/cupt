use Test::More tests => 6;

require(get_rinclude_path('../common'));


my $snapshot_name = '201501';
my $snapshot_path = "var/lib/cupt/snapshots/$snapshot_name";

sub setup_cupt {
	return TestCupt::setup(
		'dpkg_status' => [
			compose_installed_record('ooo', 1) ,
			compose_installed_record('ppp', 2) ,
		],
	);
}

sub test {
	my ($corrupter, $error, $description) = @_;
	$description //= $error;

	my $cupt = setup_cupt();

	subtest $description => sub {
		save_snapshot($cupt, $snapshot_name);
		$corrupter->();
		test_snapshot_command($cupt, "load $snapshot_name", qr/^E:.* $error$/m, "error message '$error' printed");
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

test(
	sub { system("mv $snapshot_path ${snapshot_path}%"); },
	"unable to find a snapshot named '$snapshot_name'"
);

test(
	sub {
		my $pfile = glob("$snapshot_path/*Packages");
		system("echo '' > $pfile");
	},
	"unable to find snapshot version for the package '.*'",
	'broken Packages'
);

test(
	sub {
		my $rfile = glob("$snapshot_path/*Release");
		system("echo 'y17341732' > $rfile");
	},
	".*",
	"broken Release"
);

test(
	sub { system("echo 'deb fttps://corr $snapshot_name/' > $snapshot_path/source"); },
	".*",
	'broken snapshot repository source file'
);

