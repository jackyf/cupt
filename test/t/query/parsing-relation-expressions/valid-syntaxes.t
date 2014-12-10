use TestCupt;
use Test::More tests => 16;

use strict;
use warnings;

my $cupt = TestCupt::setup();

sub test {
	my ($relation) = @_;

	my $result = exitcode("$cupt -s satisfy '$relation-'");

	is($result, 0, "relation '$relation'");
}

test('abc');
test('xyz ');
test('xyz     ');
test('abc (= 5)');
test('abc (<< 6.4)');
test('abc (>> 6.6)');
test('abc (> 6.7)');
test('abc (< 2.m)');
test('abc (>= 12)');
test('abc (<= 2.10.8)');
test('abc (>> 8)  ');
test('abc(=5)');
test('abc    (>> 4)');
test('qw46 (>= 1)');
test('ww (>=     6)');
test('qq (= 7  )');

