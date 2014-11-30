use TestCupt;
use Test::More tests => 12;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'packages' =>
		entail(compose_package_record('aa', 0) . "Provides: pp\n") .
		entail(compose_package_record('aa', 1) . "Provides: pp (= 10)\n") .
		entail(compose_package_record('aa', 2) . "Provides: pp (= 20)\n") .
		entail(compose_package_record('aa', 3) . "Provides: pp (= 10), pp (= 20)\n") .
		entail(compose_package_record('aa', 4) . "Provides: pp (<< 40), pp (>= 000)\n") . # invalid versions --> unversioned
		entail(compose_package_record('aa', 5)) .
		entail(compose_package_record('aa', 6) . "Provides: qq\n"),
);

sub test {
	my ($relation_suffix, $expected_result) = @_;

	my $output = get_all_offers("$cupt -V satisfy 'pp $relation_suffix'");

	my @versions = map { get_offered_version($_, 'aa') } split_offers($output);

	my $comment = "relation suffix: '$relation_suffix', expected_versions: '@$expected_result'";
	is_deeply(\@versions, $expected_result, $comment) or diag($output);
}

test('' => [ qw(4 3 2 1 0) ]);

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

