use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('m', '1')) .
		entail(compose_installed_record('n', '11')) .
		entail(compose_installed_record('l', '21')) .
		entail(compose_installed_record('d', '0.99')),
	'packages' =>
		entail(compose_package_record('m', '2')) .
		entail(compose_package_record('m', '3.broken') . "Depends: m (= 2)\n") .
		entail(compose_package_record('n', '12')) .
		entail(compose_package_record('l', '22')) .
		entail(compose_package_record('l', '23')) .
		entail(compose_package_record('d', '0.98')),
);

sub get_not_preferred_regex {
	my ($line) = @_;
	return qr/not preferred.*\n\n^$line\s*$/im
}

sub our_first_offer {
	my ($cupt, $arguments) = @_;
	return get_first_offer("$cupt -o cupt::console::actions-preview::show-versions=no $arguments");
}

like(our_first_offer($cupt, "install --show-not-preferred"), get_not_preferred_regex('l m n'), 'm and n and l can be upgraded');
like(our_first_offer($cupt, "safe-upgrade"), get_not_preferred_regex('m'), 'm cannot be upgraded to the latest version');

