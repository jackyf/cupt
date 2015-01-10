use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

sub test {
	my ($package, $description) = @_;

	my $cupt = TestCupt::setup(
		'packages' =>
			entail(compose_package_record('aa', 1)),
		'extended_states' =>
			"Package: $package\nMy-Good-Tag: 8\n\n",
	);

	my $output = stdall("$cupt show aa");

	like($output, qr/^Package: aa\n/, $description);
}

test('aa', 'known package');
test('xyz', 'unknown package');

