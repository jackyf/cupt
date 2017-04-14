use Test::More tests => 8;

my $cupt = setup(
	'dpkg_status' => [
		compose_installed_record('aa', 0) . "Depends: xx, yy (>> 1), pp | qq\nPre-Depends: rr\n" ,
		compose_installed_record('xx', 1) . "Depends: zz (<= 4)\n" ,
		compose_installed_record('yy', 2) ,
		compose_installed_record('zz', 3) ,
		compose_installed_record('pp', 4) ,
		compose_installed_record('qq', 5) ,
		compose_installed_record('rr', 6) ,
	],
	'extended_states' => [
		compose_autoinstalled_record('xx') ,
		compose_autoinstalled_record('yy') ,
		compose_autoinstalled_record('zz') ,
		compose_autoinstalled_record('pp') ,
		compose_autoinstalled_record('qq') ,
		compose_autoinstalled_record('rr') ,
	],
);

eval get_inc_code('common');

sub test {
	my ($package, $expected_output) = @_;

	test_why($cupt, $package, '', $expected_output, ($package || '<no arguments>'));
}

test('aa' => '');
test('xx' => "aa 0: Depends: xx\n");
test('yy' => "aa 0: Depends: yy (>> 1)\n");
test('zz' => "aa 0: Depends: xx\nxx 1: Depends: zz (<= 4)\n");
test('pp' => "aa 0: Depends: pp | qq\n");
test('qq' => "aa 0: Depends: pp | qq\n");
test('rr' => "aa 0: Pre-Depends: rr\n");

test('' => "E: no binary package expressions specified\nE: error performing the command 'why'\n");

