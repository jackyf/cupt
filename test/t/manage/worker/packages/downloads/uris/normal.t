use Test::More tests => 1;

require(get_rinclude_path('common'));

test_uris_for_bbb_and_ccc(
	'comment' => 'planned downloads',
	'releases' => [
		{
			'packages' => [
				compose_package_record('bbb', 3) . "Filename: xxx/yyy.deb\n" ,
				compose_package_record('ccc', 4) . "Filename: pool/aux/c/ccc/ccc_4_testarch.deb\n" ,
			],
			'scheme' => 'http',
			'hostname' => 'ftp.fi.debian.org/debian',
		},
	],
	'expected_bbb' => 'http://ftp.fi.debian.org/debian/xxx/yyy.deb',
	'expected_ccc' => 'http://ftp.fi.debian.org/debian/pool/aux/c/ccc/ccc_4_testarch.deb',
);

