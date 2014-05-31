use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $packages =
		entail(compose_package_record('abc', '1') . "Depends: xyz\n") .
		entail(compose_package_record('abc', '2') . "Depends: xyz (>= 20)\n") .
		entail(compose_package_record('xyz', '10')) .
		entail(compose_package_record('xyz', '20')) .
		entail(compose_package_record('xyz', '30'));

my $cupt = TestCupt::setup('packages' => $packages);

sub get_rdepends_lines {
	my ($output) = @_;
	my @lines = split(/\n/, $output);
	shift @lines;
	return @lines;
}

subtest "not satisfying versionful" => sub {
	my $output = `$cupt rdepends xyz=10`;
	my @lines = get_rdepends_lines($output);

	is(scalar @lines, 1, "1 answer") or
			return diag($output);
	like($lines[0], qr/abc 1/, "abc 1 present");
};

subtest "satisfying versionful" => sub {
	my $output = `$cupt rdepends xyz=30`;
	my @lines = get_rdepends_lines($output);

	is(scalar @lines, 2, "2 answers") or
			return diag($output);
	like($lines[0], qr/abc 1/, "first line is 'abc 1'");
	like($lines[1], qr/abc 2/, "second line is 'abc 2'");
};

