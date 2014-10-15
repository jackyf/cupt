use TestCupt;
use Test::More tests => 1;

use strict;
use warnings;

sub setup_cupt {
	return TestCupt::setup(
		'dpkg_status' =>
			entail(compose_installed_record('abc', '1')),
	);
}

sub test {
	my ($regex, $comment) = @_;

	my $cupt = setup_cupt();

	my $offer = get_first_offer("$cupt remove abc --no-remove");
	like($offer, $regex, $comment);
}

test(regex_no_solutions(), "--no-remove does prevent explicit removals");

