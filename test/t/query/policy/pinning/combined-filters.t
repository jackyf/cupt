use Test::More tests => 14;

require(get_rinclude_path('pinning'));

sub test {
	my ($first_pin_line, $pin_expression, $expected_result) = @_;

	test_pinning(
		{
			'package' => 'mno',
			'version' => '3.4',
			'package_comment' => '',
			'release_properties' => {
				'archive' => 'green',
				'codename' => 'blue',
			},
			'first_pin_line' => $first_pin_line,
			'pin_expression' => $pin_expression,
		},
		$expected_result
	);
}

test('Package: mno', 'version 3.4' => 1);
test('Package: mnp', 'version 3.4' => 0);
test('Package: mno', 'version 3.5' => 0);

test('Package: *', 'release a=green, n=blue' => 1);
test('Package: *', 'release a=red, n=blue' => 0);
test('Package: *', 'release a=green, n=red' => 0);

test('Package: mno', 'release a=green, n=blue' => 1);
test('Package: xno', 'release a=green, n=blue' => 0);

test('Package: xyz mno', 'version *' => 1);
test('Package: mno xxx *', 'version *' => 1);
test('Package: *m* *a*', 'version *' => 1);
test('Package: *n* nop *o*', 'version *' => 1);
test('Package: lll kkk', 'version *' => 0);
test('Package: *x* *y* *p* *q*', 'version *' => 0);

