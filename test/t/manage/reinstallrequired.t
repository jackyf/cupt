use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $installed = <<END;
Package: abc
Status: install reinstreq installed
Version: 1
Architecture: all

END

subtest "reinstreq without candidates" => sub {
	my $cupt = TestCupt::setup('dpkg_status' => $installed);

	my $output = get_first_offer("$cupt install -V -o debug::resolver=yes");
	like($output, regex_offer(), "there is an offer");
	like($output, qr/abc \[1\^installed\]/, "package 'abc' is removed");
};

subtest "reinstreq with candidates" => sub {
	my $packages = <<END;
Package: abc
Version: 1
Architecture: all
SHA1: 1

END

	my $cupt = TestCupt::setup('dpkg_status' => $installed, 'packages' => $packages);

	my $output = get_first_offer("$cupt install -V -o debug::resolver=yes");
	like($output, regex_offer(), "there is an offer");
	like($output, qr/abc \[1\^installed -> 1\]/, "package 'abc' is reinstalled");

	#system("cgdb -- --args $cupt install -s -V -o debug::resolver=yes");
};

