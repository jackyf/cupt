use TestCupt;
use Test::More tests => 13;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('aa', 7)) .
		entail(compose_installed_record('bb', 8)) .
		entail(compose_installed_record('mm', 6) . "Depends: xxx\n") .
		entail(compose_installed_record('xxx', 15)) ,
	'packages' =>
		entail(compose_package_record('aa', 10) . "Recommends: xxx\n") .
		entail(compose_package_record('bb', 11) . "Depends: xxx\n") .
		entail(compose_package_record('bb', 12) . "Depends: xxx (>= 14)\n") .
		entail(compose_package_record('bb', 13) . "Depends: xxx (>= 16)\n") .
		entail(compose_package_record('cc', 14)) ,
	'extended_states' =>
		entail(compose_autoinstalled_record('xxx')),
);

eval get_inc_code('common');

sub test {
	my ($from, $expected_result, $installed_only) = @_;

	$from = join(' ', map { "'$_'" } split(' ', $from));

	my $options = '';
	if ($installed_only//0) {
		$options .= '--installed-only';
	}

	test_why("$from xxx", $options, $expected_result, "from: [$from], options: [$options]");
}

test('aa' => "aa 10: Recommends: xxx\n");
test('bb' => '');
test('bb=12' => "bb 12: Depends: xxx (>= 14)\n");
test('bb=11' => "bb 11: Depends: xxx\n");
test('cc' => '');
test('mm' => "mm 6: Depends: xxx\n");
test('xxx' => '');

test('aa bb' => "aa 10: Recommends: xxx\n");
test('c*' => '');
test('??', "aa 10: Recommends: xxx\n");

test('??', "mm 6: Depends: xxx\n", 1);
test('aa', '', 1);
test('aa bb', '', 1);

