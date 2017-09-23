use Test::More tests => 4;

eval get_inc_code('common');

my $cupt = setup(
	'dpkg_status' => [
		compose_installed_record('aa', 1) ,
		compose_installed_record('bb', 2) ,
	],
	'packages' => [
		compose_package_record('bb', 3) ,
		compose_package_record('cc', 4) ,
	],
);
test_dpkg_sequence($cupt, 'remove aa --install bb cc # action type priority',
		['--install', [], ['<bb 3>']],
		['--install', [], ['<cc 4>']],
		['--remove', [], ['aa']]);

$cupt = setup(
	'packages' =>
		entail(compose_package_record('dd', 4)) .
		entail(compose_package_record('aa', 1)) .
		entail(compose_package_record('ee', 0)) .
		entail(compose_package_record('cc', 3)) .
		entail(compose_package_record('bb', 2)) ,
);
for my $spelling ('"*"', 'ee dd cc bb aa', 'dd aa ee cc bb') {
	test_dpkg_sequence($cupt, "install $spelling # alphabetic package name priority",
			['--install', [], ['<aa 1>']],
			['--install', [], ['<bb 2>']],
			['--install', [], ['<cc 3>']],
			['--install', [], ['<dd 4>']],
			['--install', [], ['<ee 0>']]);
}

