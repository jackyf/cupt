use TestCupt;
use Test::More tests => 4;

use strict;
use warnings;

my $packages =
		entail(compose_package_record('xyz', '100')) .
		entail(compose_package_record('zzz', '1') . "Depends: xyz\n") .
		entail(compose_package_record('xxx', '2') . "Depends: xyz\n") .
		entail(compose_package_record('yyy', '10') . "Depends: xyz\n");

my $cupt = TestCupt::setup('packages' => $packages);

sub get_rdepends_lines {
	my ($output) = @_;
	my @lines = split(/\n/, $output);
	shift @lines;
	return @lines;
}

my $output = stdout("$cupt rdepends xyz");
my @lines = get_rdepends_lines($output);

is(scalar @lines, 3, "3 answers") or
		return diag($output);
like($lines[0], qr/xxx/, "first line is 'xxx'");
like($lines[1], qr/yyy/, "second line is 'yyy'");
like($lines[2], qr/zzz/, "third line is 'zzz'");

