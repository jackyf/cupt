use TestCupt;
use Test::More tests => 1;

use strict;
use warnings;

eval get_inc_code('common');

test_uris(
	'comment' => 'planned downloads',
	'packages2' => [
		{
			'content' =>
				entail(compose_package_record('bbb', 3) . "Filename: xxx/yyy.deb\n") .
				entail(compose_package_record('ccc', 4) . "Filename: pool/aux/c/ccc/ccc_4_testarch.deb\n"),
			'scheme' => 'http',
			'hostname' => 'ftp.fi.debian.org/debian',
		},
	],
	'expected_bbb' => 'http://ftp.fi.debian.org/debian/xxx/yyy.deb',
	'expected_ccc' => 'http://ftp.fi.debian.org/debian/pool/aux/c/ccc/ccc_4_testarch.deb',
);

