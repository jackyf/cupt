use TestCupt;
use Test::More tests => 10;

use strict;
use warnings;

my $common_relations = "Provides: other_tt\nRecommends: missing-package\n";

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('tt', '1')),
	'packages' =>
		entail(compose_package_record('tt', '2') . $common_relations) .
		entail(compose_package_record('tt', '0') . $common_relations) ,
);

sub get_first_offer_for {
	my ($score) = @_;
	my $score_option = defined $score ? "-o cupt::resolver::score::version-factor::common=$score" : '';
	return get_first_offer("$cupt install --importance=150 --satisfy other_tt $score_option");
}

sub test {
	my ($score, $expected_tt_version) = @_;
	my $comment = 'score: ' . ($score//'default') . ", expecting $expected_tt_version";
	my $offer = get_first_offer_for($score);
	is(get_offered_version($offer, 'tt'), $expected_tt_version, $comment) or diag($offer);
}

test(undef, 2);

test(-500, 0);
test(-100, 0);
test(-30, 0);
test(0, get_unchanged_version());
test(10, get_unchanged_version());
test(50, 2);
test(100, 2);
test(200, 2);
test(500, 2);

