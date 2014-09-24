use TestCupt;
use Test::More tests => 1;

use strict;
use warnings;

my @dependee_packages;

sub generate_r_record {
	my ($index) = @_;

	my @r_packages = ("r$index-3", "r$index-7", "r$index-5");
	push @dependee_packages, @r_packages;

	my $depends_line = 'Depends: ' . join(', ', @r_packages) . "\n";
	return entail(compose_installed_record(sprintf("a%02d", $index), 1) . $depends_line);
}

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		join('', map { generate_r_record($_) } (1..42)),
	'packages' =>
		join('', map { entail(compose_package_record($_, 0)) } @dependee_packages),
);

my $log = get_first_offer("$cupt install -o debug::resolver=yes");
my @log_lines = split("\n", $log);

my @problems_in_order = map { m/problem .* depends '(.+?)'/ } @log_lines;

is_deeply(\@problems_in_order, [ reverse @dependee_packages ]) or diag($log);

