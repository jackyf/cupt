use TestCupt;
use Test::More tests => 4;

use strict;
use warnings;

my $cupt;
eval get_inc_code('common');

$cupt = setup_for_worker(
	'dpkg_status' =>
		entail(compose_installed_record('cc', 3)) ,
	'packages' =>
		entail(compose_package_record('aa', 1) . "Pre-Depends: bb\n") .
		entail(compose_package_record('bb', 2) . "Breaks: cc\n") ,
);
test_dpkg_sequence('install aa' =>
		['--remove', [], ['cc']],
		['--install', [], ['<bb 2>']],
		['--install', [], ['<aa 1>']]);

$cupt = setup_for_worker(
	'dpkg_status' =>
		entail(compose_installed_record('lockstep-master', 1) . "Depends: lockstep-slave (= 1)\n") .
		entail(compose_installed_record('lockstep-slave', 1)) ,
	'packages' =>
		entail(compose_package_record('lockstep-master', 2) . "Depends: lockstep-slave (= 2)\n") .
		entail(compose_package_record('lockstep-slave', 2)) ,
);
test_dpkg_sequence('install lockstep-master' =>
		['--unpack', [], ['<lockstep-master 2>']],
		['--install', [], ['<lockstep-slave 2>']],
		['--configure', [], ['lockstep-master']]);

$cupt = setup_for_worker(
	'dpkg_status' =>
		entail(compose_installed_record('libx', 1)) ,
	'packages' =>
		entail(compose_package_record('libx', 2) . "Depends: libx-common\n") .
		entail(compose_package_record('libx-common', 3) . "Conflicts: libx (<< 2)\n") ,
);
test_dpkg_sequence('install libx' =>
		['--unpack', [], ['<libx 2>']],
		['--install', [], ['<libx-common 3>']],
		['--configure', [], ['libx']]);

$cupt = setup_for_worker(
	'dpkg_status' =>
		entail(compose_installed_record('predepends-lockstep-master', 4) . "Pre-Depends: slave (= 4)\n") .
		entail(compose_installed_record('slave', 4)) ,
	'packages' =>
		entail(compose_package_record('predepends-lockstep-master', 5) . "Pre-Depends: slave (= 5)\n") .
		entail(compose_package_record('slave', 5)),
);
test_dpkg_sequence('install predepends-lockstep-master' =>
		['--remove', [], ['predepends-lockstep-master']],
		['--install', [], ['<slave 5>']],
		['--install', [], ['<predepends-lockstep-master 5>']]);
