use TestCupt;
use Test::More tests => 1;

use strict;
use warnings;

my $packages2 = [
	{
		'content' =>
			entail(compose_package_record('bbb', 3) . "Filename: pool/b/bbb5.deb\n") .
			entail(compose_package_record('ccc', 4) . "Filename: pool/c/ccc4.deb\n"),
		'scheme' => 'https',
		'hostname' => 'debs.net',
	},
];
my $debdelta_conf = <<END;
[our archive]
delta_uri=http://deltas.info/pub

END
my $expected_bbb = 'https://debs.net/pool/b/bbb5.deb | debdelta:http://deltas.info/pub/pool/b/bbb_2_3_all.debdelta';
my $expected_ccc = 'https://debs.net/pool/c/ccc4.deb';

eval get_inc_code('common');

