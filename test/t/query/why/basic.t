use TestCupt;
use Test::More tests => 7;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('aa', 0) . "Depends: xx, yy (>> 1), pp | qq\nPre-Depends: rr\n") .
		entail(compose_installed_record('xx', 1) . "Depends: zz (<= 4)\n") .
		entail(compose_installed_record('yy', 2)) .
		entail(compose_installed_record('zz', 3)) .
		entail(compose_installed_record('pp', 4)) .
		entail(compose_installed_record('qq', 5)) .
		entail(compose_installed_record('rr', 6)) ,
	'extended_states' =>
		entail(compose_autoinstalled_record('xx')) .
		entail(compose_autoinstalled_record('yy')) .
		entail(compose_autoinstalled_record('zz')) .
		entail(compose_autoinstalled_record('pp')) .
		entail(compose_autoinstalled_record('qq')) .
		entail(compose_autoinstalled_record('rr')) ,
);

eval get_inc_code('common');

sub test {
	my ($package, $expected_output) = @_;

	test_why($package, '', $expected_output, $package);
}

test('aa' => '');
test('xx' => "aa 0: Depends: xx\n");
test('yy' => "aa 0: Depends: yy (>> 1)\n");
test('zz' => "aa 0: Depends: xx\nxx 1: Depends: zz (<= 4)\n");
test('pp' => "aa 0: Depends: pp | qq\n");
test('qq' => "aa 0: Depends: pp | qq\n");
test('rr' => "aa 0: Pre-Depends: rr\n");

