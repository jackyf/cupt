use Test::More tests => 2;

my $cupt = setup(
	'dpkg_status' => [
		compose_installed_record('bb', 1) . "Depends: cc, dd (>= 2), dd\n" ,
		compose_installed_record('aa', 1) . "Depends: cc\n" ,
		compose_installed_record('cc', 3) ,
		compose_installed_record('dd', 4) ,
	],
	'extended_states' => [
		compose_autoinstalled_record('cc') ,
		compose_autoinstalled_record('dd') ,
	]
);

eval get_inc_code('common');

test_why($cupt, 'cc', '', "aa 1: Depends: cc\n", 'for packages -- alphabetic order');
test_why($cupt, 'dd', '', "bb 1: Depends: dd (>= 2)\n", 'for relation expressions -- left-to-right order');

