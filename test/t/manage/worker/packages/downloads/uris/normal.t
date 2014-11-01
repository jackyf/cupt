use TestCupt;
use Test::More tests => 1;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('dpkg', 0)) .
		entail(compose_installed_record('aaa', 1)) .
		entail(compose_installed_record('bbb', 2)),
	'packages2' =>
		[
			{
				'content' => 
					entail(compose_package_record('bbb', 3) . "Filename: xxx/yyy.deb\n") .
					entail(compose_package_record('ccc', 4) . "Filename: pool/aux/c/ccc/ccc_4_testarch.deb\n"),
				'scheme' => 'http',
				'hostname' => 'ftp.fi.debian.org/debian',
			},
		]
);

my $output = stdall("$cupt -s -y install bbb ccc --remove aaa");

my @downloads = ($output =~ m/^S: downloading: (.*)$/mg);

subtest 'planned downloads' => sub {
	is(scalar @downloads, 2, "2 downloads are planned");
	TODO: {
		local $TODO = 'debdelta configuration should be read from relative paths, not absolute';
		is($downloads[0], 'http://ftp.fi.debian.org/debian/xxx/yyy.deb', 'download of bbb');
	}
	is($downloads[1], 'http://ftp.fi.debian.org/debian/pool/aux/c/ccc/ccc_4_testarch.deb', 'download of ccc');
} or diag($output);

