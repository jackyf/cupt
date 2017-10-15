use TestCupt;
use Test::More tests => 3;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('i', 1)),
	'packages' => 
		entail(compose_package_record('n1', 3) . "Depends: p\n") .
		entail(compose_package_record('i', 2) . "Provides: p\n") .
		entail(compose_package_record('n2', 4) . "Provides: p\n"),
);

sub test {
	my ($new_score, $expected_n1, $expected_n2) = @_;

	my $new_option = defined $new_score ? "-o cupt::resolver::score::new=$new_score" : '';
	my $offer = get_first_offer("$cupt --importance=10 install n1 $new_option");

	my $score_comment = ($new_score // 'default');

	subtest "new score: $score_comment" => sub {
		is(get_offered_version($offer, 'n1'), ($expected_n1 ? '3' : get_unchanged_version()), "n1 expected: $expected_n1");
		is(get_offered_version($offer, 'n2'), ($expected_n2 ? '4' : get_unchanged_version()), "n2 expected: $expected_n2");
	} or diag($offer);
}

test(-20, 0, 0);
test(undef, 1, 0);
test(400, 1, 1);

