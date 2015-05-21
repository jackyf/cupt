use TestCupt;
use Test::More tests => 3;

use strict;
use warnings;

require(get_rinclude_path('common'));

my $cupt = setup_for_worker(
	'packages2' => [
		{
			'content' =>
				entail(compose_package_record('lll', 0) . "Filename: pool/0.deb\n") .
				entail(compose_package_record('mmm', 3) . "Filename: pool/1.deb\n") ,
			'scheme' => 'https',
			'hostname' => 'qq.uu',
		},
		{
			'content' =>
				entail(compose_package_record('lll', 0) . "Filename: pool/0.deb\n") .
				entail(compose_package_record('mmm', 3) . "Filename: pool/2.deb\n") ,
			'scheme' => 'ftp',
			'hostname' => 'ftp.ret',
		},
		{
			'content' => entail(compose_package_record('mmm', 3, 'sha' => 'fed') . "Filename: pool/3.deb\n"),
			'scheme' => 'http',
			'hostname' => 'blabla.uu',
		}
	]
);

test_uris('2 uris for lll 0, same filename', $cupt, 'install lll', [ 'https://qq.uu/pool/0.deb | ftp://ftp.ret/pool/0.deb' ]);
TODO: {
	local $TODO = 'not implemented';
	test_uris('2 uris for mmm 3, different filenames', $cupt, 'install mmm', [ 'https://qq.uu/pool/1.deb | ftp://ftp.ret/pool/2.deb' ]);
}
test_uris('1 uri for "another" mm 3', $cupt, 'install "Ru(.*blabla.*)"', [ 'http://blabla.uu/pool/3.deb' ]);

