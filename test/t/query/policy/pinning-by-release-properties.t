use TestCupt;
use Test::More tests => 22;

use strict;
use warnings;

eval(get_inc_code('pinning'));

sub test {
	my ($property, $expected_result) = @_;

	test_pinning(
		{
			'package' => 'zzz',
			'version' => 3,
			'release_properties' => {
				'archive' => 'aaaaa',
				'codename' => 'ccccc',
				'label' => 'SuperSecret',
				'component' => 'contrib',
			},
			'package_content' => '',
			'package_comment' => '',
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

test('l=SuperSecret' => 1);
test('l=Secret' => 0);
test('l=supersecret' => 0);
test('l=/upe/' => 1);
test('l=/upp/' => 0);
test('l=*Secret*' => 1);
test('l=Secret*' => 0);
test('laskdfasdfl=%&^...*' => -1);

test('c=contrib' => 1);
test('c=main' => 0);
test('c=non-free' => 0);
test('c=garbagfzz' => 0);
test('c=/main|contrib/' => 1);
test('c=/+/' => -1);

