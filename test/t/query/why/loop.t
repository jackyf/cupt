use TestCupt;
use Test::More tests => 3;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('aa', 1) . "Depends: bb\n") .
		entail(compose_installed_record('bb', 2) . "Depends: cc\n") .
		entail(compose_installed_record('cc', 3) . "Depends: bb\n") .
		entail(compose_installed_record('xx', 4)) ,
	'extended_states' =>
		entail(compose_autoinstalled_record('bb')) .
		entail(compose_autoinstalled_record('cc')) .
		entail(compose_autoinstalled_record('xx')) ,

);

eval get_inc_code('common');

sub test {
	my ($package, $expected_result) = @_;
	test_why($package, '', $expected_result, $package);
}

test('bb', "aa 1: Depends: bb\n");
test('cc', "aa 1: Depends: bb\nbb 2: Depends: cc\n");
test('xx', '');

