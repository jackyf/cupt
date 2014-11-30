use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $aa_provides = "Provides: nn, ll\n";
my $bb_provides = "Provides: pp (= 3), ll, qq (= 2:1.3~4+bpo5)\n";

my $cupt = TestCupt::setup(
	'packages' =>
		entail(compose_package_record('aa', 1) . $aa_provides) .
		entail(compose_package_record('bb', 1) . $bb_provides)
);

sub test {
	my ($package, $expected_provides_line) = @_;

	my $comment = "package: $package, expected provides line: $expected_provides_line";

	my $output = stdout("$cupt show $package");
	like($output, qr/^\Q$expected_provides_line\E/m, $comment);
}

test('aa', $aa_provides);
test('bb', $bb_provides);

