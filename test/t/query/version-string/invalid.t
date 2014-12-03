use TestCupt;
use Test::More;

use strict;
use warnings;

my @incorrect_version_strings = (
		'ab:5', ':1.2.3', '2a5:1.2', # broken epoch
		'1.2.3-a:6', '1.2-:5', # broken revision
		'', '$', '2в.3.4', '5.2.5&', '%%', '()', '2.6.7!!!', # broken upstream version
		# 'abc' is excluded from here, people use it... :(
);

plan tests => scalar @incorrect_version_strings;

sub test {
	my $version = shift;

	my $cupt = TestCupt::setup(
		'packages' =>
			entail(compose_package_record('aa', $version))
	);

	my $output = stdall("$cupt show aa");

	like($output, qr/^E: invalid version string '\Q$version\E'$/m, $version);
}

foreach (@incorrect_version_strings) {
	test($_);
}

