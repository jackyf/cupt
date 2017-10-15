use TestCupt;
use Test::More tests => 6;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('pi', '1.2-3') . "Depends: nnn (>= 4)\n"),
	'packages' =>
		entail(compose_package_record('nnn', '3')) .
		entail(compose_package_record('pi', '1.4-1') . "Depends: nnn (>= 5)\n") .
		entail(compose_package_record('pi', '1.2-5') . "Depends: nnn (>= 4)\n") .
		entail(compose_package_record('pi', '1.2-6') . "Recommends: nnn (>= 4)\n") .
		entail(compose_package_record('pi', '1.2-2') . "Depends: nnn (>= 3)\n") .
		entail(compose_package_record('pi', '1.2-3') . "Depends: nnn (>= 2)\n") ,
);

my $output = get_all_offers("$cupt --no-remove install -o cupt::resolver::max-leaf-count=6 -o debug::resolver=yes");

like($output, regex_offer(), "resolving succeeded") or diag($output);

my @offers = split_offers($output);
my @offered_versions = map { get_offered_version($_, 'pi') } @offers;

sub test_found {
	my ($version, $expected_result) = @_;

	my $result = grep { $_ eq $version } @offered_versions;
	is($result, $expected_result, "version $version is offered: $expected_result") or diag($output);
}

test_found('1.4-1' => 0);
test_found('1.2-5' => 0);
test_found('1.2-6' => 1);
test_found('1.2-2' => 1);
test_found('1.2-3' => 1);

