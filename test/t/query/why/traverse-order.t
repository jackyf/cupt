use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('bb', 1) . "Depends: cc, dd (>= 2), dd\n") .
		entail(compose_installed_record('aa', 1) . "Depends: cc\n") .
		entail(compose_installed_record('cc', 3)) .
		entail(compose_installed_record('dd', 4)) ,
	'extended_states' =>
		entail(compose_autoinstalled_record('cc')) .
		entail(compose_autoinstalled_record('dd')) ,
);

eval get_inc_code('common');

test_why($cupt, 'cc', '', "aa 1: Depends: cc\n", 'for packages -- alphabetic order');
test_why($cupt, 'dd', '', "bb 1: Depends: dd (>= 2)\n", 'for relation expressions -- left-to-right order');

