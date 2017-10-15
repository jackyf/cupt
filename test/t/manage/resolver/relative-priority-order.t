use TestCupt;
use Test::More tests => 4;

use strict;
use warnings;

sub setup_cupt {
	my ($base_priority) = @_;

	return TestCupt::setup(
		'dpkg_status' =>
			entail(compose_installed_record('pp', 1)),
		'packages' =>
			entail(compose_package_record('pp', 2)) .
			entail(compose_package_record('pp', 3)) .
			entail(compose_package_record('pp', 4)),
		'preferences' =>
			compose_version_pin_record('pp', 1, $base_priority) .
			compose_version_pin_record('pp', 2, $base_priority+100) .
			compose_version_pin_record('pp', 3, $base_priority-100) .
			compose_version_pin_record('pp', 4, $base_priority)
	);
}

sub test {
	my ($base_priority) = @_;

	my $cupt = setup_cupt($base_priority);
	my $output = get_all_offers("$cupt satisfy 'pp (>= 2)'");

	my @offered_versions = map { get_offered_version($_, 'pp') } split_offers($output);

	is_deeply(\@offered_versions, [ 2, 4, 3 ], "base priority: $base_priority, priority order is preserved")
			or diag($output);
}

test(500);
test(0);
test(7000);
test(-15000);

