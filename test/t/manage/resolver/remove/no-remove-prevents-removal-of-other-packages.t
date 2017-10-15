use TestCupt;
use Test::More tests => 2;

use warnings;
use strict;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('xyz', 1)) .
		entail(compose_installed_record('abc', 20)) .
		entail(compose_installed_record('def', 30)),
	'packages' =>
		entail(compose_package_record('def', 31)) .
		entail(compose_package_record('xyz', 2) . "Breaks: abc, def\n"),
);

sub test {
	my ($command) = @_;

	my $options = '-o cupt::resolver::score::removal=100000 -o debug::resolver=yes';
	my $offer = get_first_offer("$cupt $command $options");

	subtest "$command" => sub {
		like($offer, regex_offer(), "resolving succeeded");
		is(get_offered_version($offer, 'abc'), get_unchanged_version(), "'abc' (not upgradeable) doesn't change");
		is(get_offered_version($offer, 'def'), 31, "'def' is upgraded");
	} or diag($offer)
}

test('install --no-remove --try xyz=2 --wish def=31');
test('safe-upgrade');

