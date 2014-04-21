use TestCupt;
use Test::More tests => 3;

use strict;
use warnings;

my $dpkg_status = <<END;
Package: xyz1
Status: install ok installed
Source: xyz-source
Version: 1
Architecture: all

Package: xyz2
Status: install ok installed
Source: xyz-source
Version: 2
Architecture: all

Package: xyz3
Status: install ok installed
Source: xyz-source
Version: 1
Architecture: all
END

my $packages = <<END;
Package: xyz1
Source: xyz-source
Version: 2
Architecture: all
SHA1: 1

Package: xyz2
Source: xyz-source
Version: 2
Architecture: all
SHA1: 2

Package: xyz3
Source: xyz-source
Version: 2
Architecture: all
SHA1: 3
END

my $sources = <<END;
Package: xyz-source
Version: 2
Architecture: all
Binary: xyz1, xyz2, xyz3
END

my $cupt = TestCupt::setup(
		'dpkg_status' => $dpkg_status,
		'packages' => $packages,
		'sources' => $sources
);

my $output = get_first_offer("$cupt install xyz1 -V -o cupt::resolver::synchronize-by-source-versions=soft");

like($output, regex_offer(), "resolving succeeded");
like($output, qr/xyz3 .* -> 2/, "'xyz3' package is updated");
unlike($output, qr/xyz2/, "'xyz2' package is not touched");

