use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $dpkg_status = <<END;
Package: xyz
Status: install ok installed
Version: 1
Architecture: all

END

my $packages = <<END;
Package: xyz
Version: 2
Architecture: all
SHA1: 1

Package: xyz
Version: 2
Architecture: all
SHA1: 2

END

my $cupt = TestCupt::setup(
		'dpkg_status' => $dpkg_status,
		'packages' => $packages,
);

my $output = `yes 'N' | $cupt -s remove --sf xyz/installed 2>&1`;

like($output, regex_offer(), "resolving succeeded");

my $r = regex_offer();
my @offer_count = ($output =~ /$r/g);

is(scalar @offer_count, 3, "three solutions (removal + two of 'xyz 2') are offered") or
		diag $output;

