use TestCupt;
use Test::More tests => 1;

use strict;
use warnings;

my $problem_package = 'pr';

sub compose_rdepending_package {
	my ($package, $version) = @_;
	return entail(compose_installed_record($package, $version) . "Depends: $problem_package\n");
}

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		compose_rdepending_package('yyy', 5) .
		compose_rdepending_package('zzz', 3) .
		compose_rdepending_package('aaa', 4) .
		compose_rdepending_package('bbb', 2) .
		compose_rdepending_package('xxx', 9),
);

my $log = get_first_offer("$cupt install -o debug::resolver=yes");

my @log_lines = split("\n", $log);
my @packages_in_order = map { m/problem .*?: (.+?) .+?: depends '$problem_package'/ } @log_lines;

is_deeply(\@packages_in_order, [ qw(zzz yyy xxx bbb aaa) ]) or diag($log);
		
