use Test::More tests => 3;

require(get_rinclude_path('common'));

my $cupt = setup(
	'releases' => [
		{
			'packages' => [
				compose_package_record('lll', 0) . "Filename: pool/0.deb\n" ,
				compose_package_record('mmm', 3) . "Filename: pool/1.deb\n" ,
			],
			'scheme' => 'https',
			'hostname' => 'qq.uu',
		},
		{
			'packages' => [
				compose_package_record('lll', 0) . "Filename: pool/0.deb\n" ,
				compose_package_record('mmm', 3) . "Filename: pool/2.deb\n" ,
			],
			'scheme' => 'ftp',
			'hostname' => 'ftp.ret',
		},
		{
			'packages' => [ compose_package_record('mmm', 3, 'sha' => 'fed') . "Filename: pool/3.deb\n" ],
			'scheme' => 'http',
			'hostname' => 'blabla.uu',
		}
	]
);

test_uris('2 uris for lll 0, same filename', $cupt, 'install lll', [ 'https://qq.uu/pool/0.deb | ftp://ftp.ret/pool/0.deb' ]);
test_uris('2 uris for mmm 3, different filenames', $cupt, 'install mmm', [ 'https://qq.uu/pool/1.deb | ftp://ftp.ret/pool/1.deb' ]);
test_uris('1 uri for "another" mm 3', $cupt, 'install "Ru(.*blabla.*)"', [ 'http://blabla.uu/pool/3.deb' ]);

