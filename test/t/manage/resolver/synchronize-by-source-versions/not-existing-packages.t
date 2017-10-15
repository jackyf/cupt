use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $source_line = "Source: xyz-source\n";
my $dpkg_status = entail(compose_installed_record('xyz1', '1') . $source_line);
my $packages = entail(compose_package_record('xyz1', '2') . $source_line);
my $sources = <<END;
Package: xyz-source
Version: 2
Architecture: all
Binary: xyz1, klm, abc
END

my $cupt = TestCupt::setup(
		'dpkg_status' => $dpkg_status,
		'packages' => $packages,
		'sources' => $sources
);

my $output = get_first_offer("$cupt install xyz1 -o cupt::resolver::synchronize-by-source-versions=hard");

like($output, regex_offer(), "resolving succeeded, not existing packages doesn't cause problems");
like($output, qr/xyz1 .* -> 2/, "'xyz1' package is updated");

