use Test::More tests => 3;

sub test {
	my ($text, $stanza, $expected_version) = @_;

	my $cupt = setup(
		'dpkg_status' => [ compose_installed_record('pp', 3) ],
		'packages' => [
			compose_package_record('pp', 3, 'sha' => '5') . $stanza ,
			compose_package_record('pp', 3, 'sha' => '7') ,
		]
	);

	my $offer = get_first_offer("$cupt reinstall pp");
	is(get_offered_version($offer, 'pp'), $expected_version, $text) or diag($offer);
}

test("equal candidates, first is chosen", "Suggests: xx\n" => '3');
test("first candidate is uninstallable, second is chosen", "Depends: xx\n" => '3^dhs0');
test("first candidate is worse, second is chosen", "Recommends: xx\n" => '3^dhs0');

