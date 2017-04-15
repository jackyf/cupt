use Test::More tests => 12;

eval(get_inc_code('pinning'));

sub test {
	my ($first_line, $expected_result) = @_;

	test_pinning(
		{
			'package' => 'def',
			'version' => 1,
			'package_content' => "Source: defsource\n",
			'package_comment' => 'def/defsource',
			'first_pin_line' => $first_line,
			'pin_expression' => 'version *',
		},
		$expected_result
	);
}

test('Package: def' => 1);
test('Package: d*' => 1);
test('Package: fed' => 0);
test('Packag: def' => -1);
test('Packagedef' => -1);

test('Source: defsource' => 1);
test('Source: fedsource' => 0);
test('Source: *' => 1);
test('Source: /e/' => 1);
test('Source: /g/' => 0);
test('Souce: defsource' => -1);
test('Source%&(&--^*NO CARRIER' => -1);

