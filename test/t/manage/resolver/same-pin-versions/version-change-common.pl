use TestCupt;
use Test::More tests => 8;

use strict;
use warnings;

my $installed_version_addendum;
my $command;

my $middle_candidate_exists;

sub setup_cupt {
	my ($is_installed_version_available, $is_preferred_candidate_broken) = @_;

	my $preferred_candidate_addendum = ($is_preferred_candidate_broken ? "Depends: noni\n" : '');
	return TestCupt::setup(
		'dpkg_status' =>
			entail(compose_installed_record('sh', 1). $installed_version_addendum),
		'packages' =>
			($is_installed_version_available ? entail(compose_package_record('sh', 1)) : '') .
			($middle_candidate_exists ? entail(compose_package_record('sh', 2)) : '') .
			entail(compose_package_record('sh', '2+u1') . $preferred_candidate_addendum) .
			entail(compose_package_record('sh', '2+u0')) .
			($middle_candidate_exists ? entail(compose_package_record('sh', '2+a2')) : '') ,
	);
}

sub test {
	my ($is_installed_version_available, $is_preferred_candidate_broken) = @_;
	my $cupt = setup_cupt(@_);

	my $offer = get_first_offer("$cupt $command");

	my $expected_version = $is_preferred_candidate_broken ? '2+u0' : '2+u1';
	my $comment = "middle candidate exists: $middle_candidate_exists, " .
			"installed version is available: $is_installed_version_available, " .
			"preferred candidate is broken: $is_preferred_candidate_broken";

	is(get_offered_version($offer, 'sh'), $expected_version, $comment) or diag($offer);
}

sub test_group {
	test(0, 0);
	test(1, 0);

	test(0, 1);
	test(1, 1);
}

sub run_tests {
	($installed_version_addendum, $command) = @_;

	$middle_candidate_exists = 0;
	test_group();

	$middle_candidate_exists = 1;
	test_group();
}

1;

