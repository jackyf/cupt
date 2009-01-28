#!/usr/bin/perl
BEGIN { push @INC, q(../) }

use strict;
use warnings;

use Test::More;

use Cupt::Core;

my $test_count = 0;

my @incorrect_version_strings = (
		'ab:5', ':1.2.3', '2a5:1.2', # broken epoch
		'1.2.3-a:6', '1.2-:5', # broken revision
		'', '$', '2в.3.4', '5.2.5&', '%%', '()', '2.6.7!!!', 'abc', # broken upstream version 
);
# each incorrect version is checked once
$test_count += scalar @incorrect_version_strings;

my @correct_version_strings = (
	[ '1.2.3', '1.2.3', 0 ], # identical
	[ '4.4.3-2', '4.4.3-2', 0 ], # identical
	[ '1:2ab:5', '1:2ab:5', 0 ], # this is correct...
	[ '7:1-a:b-5', '7:1-a:b-5', 0 ], # and this
	[ '57:1.2.3abYZ+~-4-5', '57:1.2.3abYZ+~-4-5', 0 ], # and those too
	[ '1.2.3', '0:1.2.3', 0 ], # zero epoch
	[ '1.2.3', '1.2.3-0', 0 ], # zero revision
	[ '009', '9', 0 ], # zeroes...
	[ '009ab5', '9ab5', 0 ], # there as well
	[ '1.2.3', '1.2.3-1', -1 ], # added non-zero revision
	[ '1.2.3', '1.2.4', -1 ], # just bigger
	[ '1.2.4', '1.2.3', 1 ], # order doesn't matter
	[ '1.2.24', '1.2.3', 1 ], # bigger, eh?
	[ '0.10.0', '0.8.7', 1 ], # bigger, eh?
	[ '3.2', '2.3', 1 ], # major number rocks
	[ '1.3.2a', '1.3.2', 1 ], # letters rock
	[ '2a', '21', -1 ], # but not in all places
	[ '1.3.2a', '1.3.2b', -1 ], # but there is another letter
	[ '1:1.2.3', '1.2.4', 1 ], # epoch rocks
	[ '1:1.2.3', '1:1.2.4', -1 ], # bigger anyway
	[ '1.2a+~bCd3', '1.2a++', -1 ], # tilde doesn't rock
	[ '1.2a+~bCd3', '1.2a+~', 1 ], # but first is longer!
	[ '5:2', '304-2', 1 ], # epoch rocks
	[ '5:2', '304:2', -1 ], # so big epoch?
	[ '25:2', '3:2', 1 ], # 25 > 3, obviously
	[ '1:2:123', '1:12:3', -1 ], # 12 > 2
	[ '1.2-5', '1.2-3-5', -1 ], # 1.2 < 1.2-3
	[ '5.10.0', '5.005', 1 ], # preceding zeroes don't matters
	[ '3a9.8', '3.10.2', -1 ], # letters are before all letter symbols
	[ '3a9.8', '3~10', 1 ], # but after the tilde
	[ '1.4+OOo3.0.0~', '1.4+OOo3.0.0-4', -1 ], # another tilde check
	[ '2.4.7-1', '2.4.7-z', -1 ], # revision comparing
	[ '1.002-1+b2', '1.00', 1 ], # whatever...
);
# each array has to be
# 1) check v1 for correctness
# 2) check v2 for correctness
# 3) check compare_version_strings
$test_count += scalar @correct_version_strings * 3;

plan tests => $test_count;

foreach (@incorrect_version_strings) {
	unlike($_, qr/^$version_string_regex$/, "incorrectness of version $_");
}
foreach my $item (@correct_version_strings) {
	my $v1 = $item->[0];
	my $v2 = $item->[1];
	like($v1, qr/^$version_string_regex$/, "correctness of version $v1");
	like($v2, qr/^$version_string_regex$/, "correctness of version $v2");

	my $expected_result = $item->[2];
	is(Cupt::Core::compare_version_strings($v1, $v2), $expected_result, "comparison of $v1 and $v2");
}
