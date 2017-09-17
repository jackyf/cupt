use TestCupt;
use Test::More tests => 6;

use strict;
use warnings;

eval get_inc_code('common');

my $cupt = setup(
	'dpkg_status' =>
		entail(compose_installed_record('bb', 2) . "Depends: dd\n") .
		entail(compose_installed_record('dd', 6)) ,
	'packages' =>
		entail(compose_package_record('aa', 1)) .
		entail(compose_package_record('bb', 3) . "Depends: dd\n") .
		entail(compose_package_record('c2', 4) . "Depends: bb (>= 3)\n") .
		entail(compose_package_record('c1', 5) . "Depends: c2\n") ,
);

test_dpkg_sequence($cupt, 'install aa' => ['--install', [], ['<aa 1>']]);
test_dpkg_sequence($cupt, 'remove bb' => ['--remove', [], ['bb']]);
test_dpkg_sequence($cupt, 'install bb' => ['--install', [], ['<bb 3>']]);

test_dpkg_sequence($cupt, 'install c2' =>
		['--install', [], ['<bb 3>']],
		['--install', [], ['<c2 4>']]);
test_dpkg_sequence($cupt, 'install c1' =>
		['--install', [], ['<bb 3>']],
		['--install', [], ['<c2 4>']],
		['--install', [], ['<c1 5>']]);
test_dpkg_sequence($cupt, 'remove dd' =>
		['--remove', [], ['bb']],
		['--remove', [], ['dd']]);

