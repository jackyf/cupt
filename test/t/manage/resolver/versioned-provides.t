use TestCupt;
use Test::More tests => 12*2;

use strict;
use warnings;

my $cupt;
my $test_mode;

sub get_cupt {
	my $with_unversioned_provides = shift;

	my $first_package = '';
	if ($with_unversioned_provides) {
		$first_package = entail(compose_package_record('aa', 0) . "Provides: pp\n");
	}

	return TestCupt::setup(
		'packages' =>
			$first_package .
			entail(compose_package_record('aa', 1) . "Provides: pp (= 10)\n") .
			entail(compose_package_record('aa', 2) . "Provides: pp (= 20)\n") .
			entail(compose_package_record('aa', 3) . "Provides: pp (= 10), pp (= 20)\n") .
			entail(compose_package_record('aa', 4) . "Provides: pp (<< 40), pp (>= 000)\n") . # invalid versions --> unversioned
			entail(compose_package_record('aa', 5)) .
			entail(compose_package_record('aa', 6) . "Provides: qq\n"),
	);
}

sub test {
	my ($relation_suffix, $expected_result) = @_;

	my $output = get_all_offers("$cupt satisfy 'pp $relation_suffix'");

	my @versions = map { get_offered_version($_, 'aa') } split_offers($output);

	my $comment = "mode: $test_mode, relation suffix: '$relation_suffix', expected_versions: '@$expected_result'";
	is_deeply(\@versions, $expected_result, $comment) or diag($output);
}

foreach my $mode (qw(1 0)) {
	$test_mode = $mode;
	$cupt = get_cupt($test_mode);

	test('' => $test_mode==1?[ qw(4 3 2 1 0) ]:[qw(4 3 2 1)]);

	test('(= 10)' => [ qw(3 1) ]);
	test('(= 20)' => [ qw(3 2) ]);
	test('(= 30)' => []);

	test('(>> 5)' => [ qw(3 2 1) ]);
	test('(>> 15)' => [ qw(3 2) ]);
	test('(>> 25)' => []);

	test('(<< 26)' => [ qw(3 2 1) ]);
	test('(<< 16)' => [ qw(3 1) ]);
	test('(<< 6)' => []);

	test('(>= 11)' => [ qw(3 2) ]);
	test('(>= 10)' => [ qw(3 2 1) ]);
}

