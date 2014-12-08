use TestCupt;
use Test::More tests => 15 + 18;

use strict;
use warnings;

sub test {
	my ($relation, $expected_error_string, $error_part) = @_;
	$expected_error_string = 'E: ' . $expected_error_string;

	my $cupt = TestCupt::setup();
	my $output = stdall("$cupt -s satisfy '$relation-'");

	my $comment = "relation '$relation' has broken $error_part";
	like($output, qr/^\Q$expected_error_string\E$/m, $comment);
}

sub get_parse_error_string {
	my ($relation, $error_part) = @_;
	return "failed to parse a $error_part in the relation '$relation'";
}

sub test_broken_package_name {
	my $relation = shift;
	my $error_part = 'package name';
	test($relation, get_parse_error_string($relation, $error_part), $error_part);
}

sub test_broken_version_info {
	my $relation = shift;
	my $error_part = 'version part';
	test($relation, get_parse_error_string($relation, $error_part), $error_part);
}

sub test_broken_version_string {
	my ($relation, $version_string) = @_;
	test($relation, "invalid version string '$version_string'", 'version string');
}

test_broken_package_name('a aa');
test_broken_package_name('a"m');
test_broken_package_name('');
test_broken_package_name('!!!');
test_broken_package_name('a123=b456');
test_broken_package_name('qwerty{uiop}');
test_broken_package_name('uu)');
test_broken_package_name('uu))');
test_broken_package_name('uu)(');
test_broken_package_name('uu)wz(');
test_broken_package_name('  abc');
test_broken_package_name("yio \\");
test_broken_package_name("\tqqq");
test_broken_package_name("mmm\n");
test_broken_package_name("\app ");

test_broken_version_info('bb (= 3');
test_broken_version_info('cc (= 4))');
test_broken_version_string('cc2 (= 4])', '4]');
test_broken_version_info('cc3 (= 2.3 _)');
test_broken_version_info('cc4 (= 2.3 z128)');
test_broken_version_string('dd (=== 5', '==');
test_broken_version_string('dd1 (=== 5)', '==');
test_broken_version_string('dd2 (===5)', '==5');
test_broken_version_info('ee ( = 3)');
test_broken_version_info('ee1 ( = 3 )');
test_broken_version_string('ff (=\t6)', '\t6');
test_broken_version_string('gg (=> 7)', '>');
test_broken_version_string('hh (<<< 8)', '<');
test_broken_version_info('ii (9)');
test_broken_version_info('ii2 (mm)');
test_broken_version_info('kk ()');
test_broken_version_info('ll (% 10)');
test_broken_version_info('ll2 ($$ 20)');


