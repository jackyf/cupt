use TestCupt;
use Test::More tests => 3;

use strict;
use warnings;

my $source_line = "Source: xyz-source\n";
my $dpkg_status =
		entail(compose_installed_record('xyz1', '1') . $source_line) .
		entail(compose_installed_record('xyz2', '2') . $source_line) .
		entail(compose_installed_record('xyz3', '1') . $source_line) .
		entail(compose_installed_record('xyz4', '1') . $source_line);
my $packages =
		entail(compose_package_record('xyz1', '2') . $source_line) .
		entail(compose_package_record('xyz2', '2') . $source_line) .
		entail(compose_package_record('xyz3', '2') . $source_line);
my $sources = <<END;
Package: xyz-source
Version: 2
Architecture: all
Binary: xyz1, xyz2, xyz3, xyz4
END

my $cupt = TestCupt::setup(
		'dpkg_status' => $dpkg_status,
		'packages' => $packages,
		'sources' => $sources
);

sub get_output {
	my ($synch_type, $params) = @_;

	return get_first_offer("$cupt install xyz1 -o cupt::resolver::synchronize-by-source-versions=$synch_type $params");
}

subtest "soft" => sub {
	my $output = get_output('soft', "");

	like($output, regex_offer(), "resolving succeeded");
	like($output, qr/xyz3 .* -> 2/, "'xyz3' package is updated");
	unlike($output, qr/xyz2/, "'xyz2' package is not touched");
	like($output, qr/xyz4: synchronization/, "'xyz4' package is not touched");
};

subtest "hard, removal possible" => sub {
	my $output = get_output('hard', "");

	like($output, regex_offer(), "resolved succeeded");
	like($output, qr/xyz3 .* -> 2/, "'xyz3' package is updated");
	like($output, qr/xyz4/, "'xyz4' package is removed");
};

subtest "hard, removal prohibited" => sub {
	my $output = get_output('hard', "--no-remove");

	like($output, regex_no_solutions(), "no solutions");
};

