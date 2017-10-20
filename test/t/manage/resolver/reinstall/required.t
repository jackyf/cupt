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

	my $output = get_first_offer("$cupt install");
	like($output, regex_offer(), "there is an offer");
	is(get_offered_version($output, 'abc'), get_empty_version(), "package 'abc' is removed");
};

subtest "reinstreq with candidates" => sub {
	my $packages = <<END;
Package: abc
Version: 1
Architecture: all
SHA1: 1

END

	my $cupt = TestCupt::setup('dpkg_status' => $installed, 'packages' => $packages);

	my $output = get_first_offer("$cupt install");
	like($output, regex_offer(), "there is an offer");
	is(get_offered_version($output, 'abc'), '1', "package 'abc' is reinstalled");
};

