use TestCupt;
use Test::More tests => 4;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('aa', 0) . "Depends: xx, yy (>> 1)\n") .
		entail(compose_installed_record('xx', 1) . "Depends: zz (<= 4)\n") .
		entail(compose_installed_record('yy', 2)) .
		entail(compose_installed_record('zz', 3)) ,
	'extended_states' =>
		entail(compose_autoinstalled_record('xx')) .
		entail(compose_autoinstalled_record('yy')) .
		entail(compose_autoinstalled_record('zz')) ,
);

sub test {
	my ($package, $expected_output) = @_;
	$expected_output =~ s/(\w+) (\d):/$1 $2^installed:/g;

	subtest $package => sub {
		my $output = stdall("$cupt why $package");
		is($?, 0, 'operation succeeded');
		is($output, $expected_output, 'output is correct');
	}
}

test('aa' => '');
test('xx' => "aa 0: Depends: xx\n");
test('yy' => "aa 0: Depends: yy (>> 1)\n");
test('zz' => "aa 0: Depends: xx\nxx 1: Depends: zz (<= 4)\n");

