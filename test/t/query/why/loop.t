use Test::More tests => 3;

my $cupt = setup(
	'dpkg_status' => [
		compose_installed_record('aa', 1) . "Depends: bb\n" ,
		compose_installed_record('bb', 2) . "Depends: cc\n" ,
		compose_installed_record('cc', 3) . "Depends: bb\n" ,
		compose_installed_record('xx', 4) ,
	],
	'extended_states' => [
		compose_autoinstalled_record('bb') ,
		compose_autoinstalled_record('cc') ,
		compose_autoinstalled_record('xx') ,
	],
);

eval get_inc_code('common');

sub test {
	my ($package, $expected_result) = @_;
	test_why($cupt, $package, '', $expected_result, $package);
}

test('bb', "aa 1: Depends: bb\n");
test('cc', "aa 1: Depends: bb\nbb 2: Depends: cc\n");
test('xx', '');

