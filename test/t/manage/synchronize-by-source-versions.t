use TestCupt;
use Test::More tests => 3;

use strict;
use warnings;

my $source_line = "Source: xyz-source\n";
my $dpkg_status =
		entail(compose_installed_record('xyz1', '1') . $source_line) .
		entail(compose_installed_record('xyz2', '2') . $source_line) .
		entail(compose_installed_record('xyz3', '1') . $source_line);
my $packages =
		entail(compose_package_record('xyz1', '2') . $source_line) .
		entail(compose_package_record('xyz2', '2') . $source_line) .
		entail(compose_package_record('xyz3', '2') . $source_line);
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

