use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'packages' =>
		entail(compose_package_record('pp', 5) . "Depends: not-existing\n") .
		entail(compose_package_record('pp', 5, 'sha' => 'abc1')) .
		entail(compose_package_record('pp', 5, 'sha' => 'abc2') . "Recommends: not-existing\n"),
);

sub test {
	my ($comment, $request, $expected_version) = @_;

	my $output = get_first_offer("$cupt install --sf $request");
	my $actual_version = get_offered_version($output, 'pp');

	is($actual_version, $expected_version, $comment)
			or (diag($output), diag(stdall("$cupt policy pp")));
}

test('best available dhs version', 'pp', '5^dhs0');
test('specifictly chosen dhs version', 'pp=5^dhs1', '5^dhs1');

