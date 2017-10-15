use TestCupt;
use Test::More tests => 11;

use strict;
use warnings;

sub setup_cupt {
	my ($new_pin) = @_;

	return TestCupt::setup(
		'dpkg_status' =>
			entail(compose_installed_record('ii', 1)),
		'packages' =>
			entail(compose_package_record('ii', 2)) .
			entail(compose_package_record('newp', 3)) .
			entail(compose_package_record('mm', 4) . "Depends: ii (>= 2) | newp\n"),
		'preferences' =>
			compose_version_pin_record('newp', '*', $new_pin),
	);
}

sub test {
	my ($new_pin, $upgrade_expected) = @_;

	my $cupt = setup_cupt($new_pin);
	my $offer = get_first_offer("$cupt install mm -o debug::resolver=yes");

	my $comment = "new package of pin $new_pin vs upgrade of existing one, upgrade expected: $upgrade_expected";

	is(get_offered_version($offer, 'ii'), ($upgrade_expected ? 2 : get_unchanged_version()), $comment)
			or diag($offer);
}

test(-1000 => 1);
test(100 => 1);
test(300 => 1);
test(500 => 1);
test(700 => 1);
test(900 => 1);
test(990 => 1);
test(1000 => 1);
test(1100 => 0);
test(2000 => 0);
test(5000 => 0);

