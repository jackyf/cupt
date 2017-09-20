use Test::More tests => 5;

require(get_rinclude_path('common'));

my $cupt = setup(
	'dpkg_status' => [ compose_installed_record('cc', 3) ],
	'packages' => [
		compose_package_record('aa', 1) . "Pre-Depends: bb\n" ,
		compose_package_record('bb', 2) . "Breaks: cc\n" ,
	],
);
test_dpkg_sequence($cupt, 'install aa' =>
		['--remove', [], ['cc']],
		['--install', [], ['<bb 2>']],
		['--install', [], ['<aa 1>']]);

$cupt = setup(
	'dpkg_status' => [
		compose_installed_record('lockstep-master', 1) . "Depends: lockstep-slave (= 1)\n" ,
		compose_installed_record('lockstep-slave', 1) ,
	],
	'packages' => [
		compose_package_record('lockstep-master', 2) . "Depends: lockstep-slave (= 2)\n" ,
		compose_package_record('lockstep-slave', 2) ,
	],
);
test_dpkg_sequence($cupt, 'install lockstep-master' =>
		['--unpack', [], ['<lockstep-master 2>']],
		['--install', [], ['<lockstep-slave 2>']],
		['--configure', [], ['lockstep-master']]);

$cupt = setup(
	'dpkg_status' => [ compose_installed_record('libx', 1) ],
	'packages' => [
		compose_package_record('libx', 2) . "Depends: libx-common\n" ,
		compose_package_record('libx-common', 3) . "Conflicts: libx (<< 2)\n" ,
	]
);
test_dpkg_sequence($cupt, 'install libx' =>
		['--unpack', [], ['<libx 2>']],
		['--install', [], ['<libx-common 3>']],
		['--configure', [], ['libx']]);

$cupt = setup(
	'dpkg_status' => [
		compose_installed_record('predepends-lockstep-master', 4) . "Pre-Depends: slave (= 4)\n" ,
		compose_installed_record('slave', 4) ,
	],
	'packages' => [
		compose_package_record('predepends-lockstep-master', 5) . "Pre-Depends: slave (= 5)\n" ,
		compose_package_record('slave', 5) ,
	],
);
test_dpkg_sequence($cupt, 'install predepends-lockstep-master' =>
		['--remove', [], ['predepends-lockstep-master']],
		['--install', [], ['<slave 5>']],
		['--install', [], ['<predepends-lockstep-master 5>']]);

$cupt = setup(
	'packages' => [
		compose_package_record('circular-dep-1', 6) . "Depends: circular-dep-2\n" ,
		compose_package_record('circular-dep-2', 7) . "Depends: circular-dep-1\n" ,
	],
);
test_dpkg_sequence($cupt, 'install circular-dep-1',
		['--unpack', [], ['<circular-dep-1 6>']],
		['--unpack', [], ['<circular-dep-2 7>']],
		['--configure', [], ['circular-dep-2', 'circular-dep-1']]);

