use Test::More tests => 13;

my $cupt = setup(
	'dpkg_status' => [
		compose_installed_record('aa', 7) ,
		compose_installed_record('bb', 8) ,
		compose_installed_record('mm', 6) . "Recommends: xxx\n" ,
		compose_installed_record('xxx', 15) ,
	],
	'packages' => [
		compose_package_record('aa', 10) . "Recommends: xxx\n" ,
		compose_package_record('bb', 11) . "Depends: xxx\n" ,
		compose_package_record('bb', 12) . "Depends: xxx (>= 14)\n" ,
		compose_package_record('bb', 13) . "Depends: xxx (>= 16)\n" ,
		compose_package_record('cc', 14) ,
	],
	'extended_states' => [ compose_autoinstalled_record('xxx') ],
);

eval get_inc_code('common');

sub test {
	my ($from, $expected_result, $installed_only) = @_;

	$from = join(' ', map { "'$_'" } split(' ', $from));

	my $options = '';
	if ($installed_only//0) {
		$options .= '--installed-only';
	}

	test_why($cupt, "$from xxx", $options, $expected_result, "from: [$from], options: [$options]");
}

test('aa' => "aa 10: Recommends: xxx\n");
test('bb' => '');
test('bb=12' => "bb 12: Depends: xxx (>= 14)\n");
test('bb=11' => "bb 11: Depends: xxx\n");
test('cc' => '');
test('mm' => "mm 6: Recommends: xxx\n");
test('xxx' => '');

test('aa bb' => "aa 10: Recommends: xxx\n");
test('c*' => '');
test('??', "aa 10: Recommends: xxx\n");

test('??', "mm 6: Recommends: xxx\n", 1);
test('aa', '', 1);
test('aa bb', '', 1);

