use TestCupt;
use Test::More tests => 5;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'packages' =>
		entail(compose_package_record('l1', '1') . "Depends: l2\n") .
		entail(compose_package_record('l2', '2') . "Depends: l3\n") .
		entail(compose_package_record('l3', '3')),
);

sub test {
	my ($limit, $expected_result) = @_;
	my $offer = get_first_offer("$cupt install l1 -o cupt::resolver::max-leaf-count=$limit");
	my $result = ($offer =~ regex_offer()) || 0;
	is($result, $expected_result, "when limit is $limit, expect $expected_result") or diag($offer);
}

test(10 => 1);
test(4 => 1);
test(3 => 1);
test(2 => 0);
test(0 => 0);

