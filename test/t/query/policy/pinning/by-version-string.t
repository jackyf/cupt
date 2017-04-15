use Test::More tests => 22;

require(get_rinclude_path('pinning'));

sub test {
	my ($version, $pin_expression, $match_expected) = @_;

	test_pinning(
		{
			'package' => 'abc',
			'version' => $version,
			'package_content' => '',
			'package_comment' => $version,
			'first_pin_line' => 'Package: abc',
			'pin_expression' => "version $pin_expression"
		},
		$match_expected
	);
}

test('2', '2', 1);
test('2', '3', 0);
test('20', '2', 0);
test('20', '0', 0);

test('20', '2*', 1);
test('20', '*', 1);
test('20', '3*', 0);
test('20', '?0', 1);
test('20', '?1', 0);
test('20', '2?', 1);
test('20', '??', 1);
test('20', '?', 0);

test('20', '/*/', -1);
test('20', '/.*/', 1);
test('20', '/./', 1);
test('20', '/3./', 0);
test('1.2.6', '/1\..\.6/', 1);
test('1.2.6', '/1\..\.7/', 0);
test('200', '/2+0/', 1);
test('200', '/2+1/', 0);
test('200', '/20+/', 1);
test('200', '/20++/', -1);

