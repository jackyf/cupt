use TestCupt;
use Test::More tests => 2;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('pi', '1.2-3') . "Depends: nnn (>= 4)\n"),
	'packages' =>
		entail(compose_package_record('nnn', '3')) .
		entail(compose_package_record('pi', '1.4-1') . "Depends: nnn (>= 5)\n") .
		entail(compose_package_record('pi', '1.2-5') . "Depends: nnn (>= 4)\n") .
		entail(compose_package_record('pi', '1.2-2') . "Depends: nnn (>= 3)\n") .
		entail(compose_package_record('pi', '1.2-3') . "Depends: nnn (>= 2)\n") .
		entail(compose_package_record('pi', '0.9')),
);

my $offers = get_all_offers("$cupt --no-remove -V install -o cupt::resolver::max-leaf-count=5 -o debug::resolver=yes");

like($offers, regex_offer(), "resolving succeeded") or diag($offers);
is(get_offer_count($offers), 3, "all suitable variants are offered") or diag($offers);

