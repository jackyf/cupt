use TestCupt;
use Test::More tests => 10;

use strict;
use warnings;

my $installed = entail(compose_installed_record('master', '10'));

my $packages = entail(compose_package_record('master', '30') . "Depends: slave (>= 2)\n");
$packages .= entail(compose_package_record('slave', '2') . "Conflicts: master\n");

my $cupt;
my $type;
sub run_test {
	my ($removal_score) = @_;

	subtest "dependees are not ${type} if not needed in a final solution (removal score: $removal_score)" => sub {
		my $options = "-o cupt::resolver::score::removal=$removal_score -o cupt::resolver::score::upgrade=100000";
		my $output = get_first_offer("$cupt full-upgrade $options");
		like($output, regex_offer(), "there is an offer") or
				return;
		unlike($output, qr/slave/, "slave is not changed");
	}
}

sub run_test_series {
	$cupt = TestCupt::setup(
			'dpkg_status' => $installed,
			'packages' => $packages,
			'preferences' => "Package: slave\nPin: version 2\nPin-Priority: 50\n");
	run_test(-5000);
	run_test(-500);
	run_test(0);
	run_test(500);
	run_test(5000);
}

$type = 'installed';
run_test_series();

$installed .= entail(compose_installed_record('slave', '1'));
$type = 'upgraded';
run_test_series();

