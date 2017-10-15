use TestCupt;
use Test::More tests => 10;

use strict;
use warnings;

sub generate_simple_packages {
	my ($count) = @_;

	my @list;
	my $content;

	foreach (1..$count) {
		my $name = "p$_";
		push @list, $name;
		$content .= entail(compose_package_record($name, '2'));
	}

	return (\@list, $content);
}

sub setup_cupt {
	my ($count) = @_;

	my ($list, $content) = generate_simple_packages($count);

	return TestCupt::setup(
		'dpkg_status' =>
			entail(compose_installed_record('a', '1')),
		'packages' =>
			entail(compose_package_record('a', '2') . "Depends: " . join(',', @$list) . "\n") .
			$content,
	);
}

sub get_first_offer_for {
	my ($cupt, $score) = @_;
	my $score_option = defined $score ? "-o cupt::resolver::score::version-factor::negative=$score" : '';
	return get_first_offer("$cupt install -t na --importance=160 a=2 -o debug::resolver=yes $score_option");
}

sub test {
	my ($count, $score, $expect_a_upgraded) = @_;

	my $expected_a_version = $expect_a_upgraded ? '2' : get_unchanged_version();
	my $comment = "count: $count, score: " . ($score//'default') . ", upgrade expected: $expect_a_upgraded";

	my $cupt = setup_cupt($count);
	my $offer = get_first_offer_for($cupt, $score);

	is(get_offered_version($offer, 'a'), $expected_a_version, $comment) or diag($offer); 
}

test(1, undef,  1);
test(2, undef,  1);
test(3, undef,  1);
test(5, undef,  1);
test(10, undef,  0);
test(20, undef,  0);

test(2, 120,  1);
test(3, 120,  0);

test(10, 24,  1);
test(15, 24,  0);

