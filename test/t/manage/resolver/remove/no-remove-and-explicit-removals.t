use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

sub setup_cupt {
	my ($is_auto) = @_;

	return TestCupt::setup(
		'dpkg_status' =>
			entail(compose_installed_record('abc', '1')),
		'extended_states' =>
			($is_auto ? compose_autoinstalled_record('abc') : ''),
	);
}

sub test {
	my ($is_auto, $regex, $comment) = @_;

	my $cupt = setup_cupt($is_auto);

	my $offer = get_first_offer("$cupt remove abc --no-remove");
	like($offer, $regex, $comment);
}

test(0, regex_no_solutions(), "--no-remove does prevent explicit removals");
test(1, regex_offer(), "--no-remove doesn't prevent removals of automatically installed packages");

