use TestCupt;
use Test::More tests => 4;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('aa', 1)) ,
);

sub save_snapshot {
	my $name = shift;

	subtest "saving snapshot $name" => sub {
		my $output = stdall("$cupt snapshot save $name");
		is($?, 0, "positive exit code")
				or diag($output);
		unlike($output, qr/^E: /m, "no errors");
	};
}

my $list_command = "$cupt snapshot list";

save_snapshot('xyz');

is(stdall($list_command), "xyz\n", "listing one snapshot");

save_snapshot('klm');

is(stdall($list_command), "klm\nxyz\n", "listing two snapshots");

