use TestCupt;
use Test::More tests => 8;

use strict;
use warnings;

my $installed;
my $packages;

sub add_problematic_package {
	my $package_name = $_[0];

	$installed .= entail(compose_installed_record($package_name, '10'));

	my $package_record = compose_package_record($package_name, '30');
	$package_record .= "Provides: toonew\n";
	$package_record .= "Conflicts: toonew\n";
	$packages .= entail($package_record);
}

sub add_simple_package {
	my $package_name = $_[0];

	$installed .= entail(compose_installed_record($package_name, '10'));
	$packages .= entail(compose_package_record($package_name, '20'));
}

sub test_with_package_count {
	my ($count) = @_;

	$installed = '';
	$packages = '';

	add_problematic_package('aproblematic');
	add_problematic_package('kproblematic');
	add_problematic_package('zproblematic');
	foreach my $counter (1..$count) {
		add_simple_package("simple$counter");
	}

	subtest "upgrade succeeds for $count" => sub {
		my $cupt = TestCupt::setup('dpkg_status' => $installed, 'packages' => $packages);

		my $output = get_first_offer("$cupt full-upgrade --wish kproblematic -o cupt::console::actions-preview::show-summary=yes");
		like($output, regex_offer(), "there is an offer") or
				return;

		my $upgraded_count = $count+1;
		like($output, qr/$upgraded_count .* packages will be upgraded/, "all simple packages are upgraded");
		like($output, qr/kproblematic \[10\^installed -> 30\]/, "of all problematic packages 'kproblematic' is selected");
	}
}

test_with_package_count(1);
test_with_package_count(10);
test_with_package_count(100);
test_with_package_count(1000);
test_with_package_count(2000);
test_with_package_count(4000);
test_with_package_count(7000);
test_with_package_count(10000);

