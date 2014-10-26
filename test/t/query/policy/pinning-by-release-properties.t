use TestCupt;
use Test::More tests => 8;

use strict;
use warnings;

eval(get_inc_code('pinning'));

sub test {
	my ($property, $expected_result) = @_;

	test_pinning(
		{
			'package' => 'zzz',
			'version' => 3,
			'archive' => 'aaaaa',
			'codename' => 'ccccc',
			'package_content' => '',
			'package_comment' => $property,
			'first_pin_line' => 'Package: *',
			'pin_expression' => "release $property",
		},
		$expected_result
	);
}

test('a=aaaaa' => 1);
test('a=aaaa' => 0);
test('a=aaaaaa' => 0);
test('aysid' => -1);

test('n=ccccc' => 1);
test('n=cccc1' => 0);
test('n=XXX' => 0);
test('nnjpi' => -1);
