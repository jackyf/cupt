use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('ie', 1) . "Essential: yes\n"),
	'packages' =>
		entail(compose_package_record('ie', 2)),
);

sub test {
	my ($score, $expected_ie_version) = @_;

	my $cupt_options = "-o cupt::resolver::score::removal-of-essential=$score -o debug::resolver=yes";
	my $upgrade_request = '--importance=500 --install ie=2';
	my $remove_request = '--importance=20000 --remove ie';
	my $offer = get_first_offer("$cupt install $upgrade_request $remove_request $upgrade_request $cupt_options");
	is(get_offered_version($offer, 'ie'), $expected_ie_version, "score: $score, expected version: $expected_ie_version")
			or diag($offer);
}

test(-15000 => get_empty_version());
test(-25000 => 2);

