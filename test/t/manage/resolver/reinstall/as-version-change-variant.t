use TestCupt;
use Test::More tests => 4;

use strict;
use warnings;

sub setup_cupt {
	my ($version, $relation) = @_;

	return TestCupt::setup(
		'dpkg_status' =>
			entail(compose_installed_record('pp', 1) . "Depends: inv\n"),
		'packages' =>
			entail(compose_package_record('pp', $version) . $relation) .
			entail(compose_package_record('pp', 1)),
		'preferences' =>
			compose_version_pin_record('pp', '0.9', 1100) .
			compose_version_pin_record('pp', '1.3', 400) ,
	);
}

sub test
{
	my ($version, $relation, $expected_result, $comment) = @_;

	my $cupt = setup_cupt($version, $relation);

	my $output = get_all_offers("$cupt install --no-remove -o debug::resolver=yes");
	my @offers = split_offers($output);

	my @offered_versions = map { get_offered_version($_, 'pp') } @offers;

	is_deeply(\@offered_versions, $expected_result, $comment) or diag($output);
}

test('0.9', ''  => [ '1', '0.9' ], 'reinstall offered before good-pin downgrade');
test('1.1', ''  => [ '1.1', '1' ], 'reinstall offered after normal-pin problemless upgrade');
test('1.3', ''  => [ '1', '1.3' ], 'reinstall offered before middle-pin problemless upgrade');
test('2', "Recommends: inv2\n"  => [ '1', '2' ], 'reinstall offered before unsatisfied-recommends upgrade');

