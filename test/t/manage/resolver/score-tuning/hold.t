use Test::More tests => 9;

require(get_rinclude_path('common'));

sub setup_cupt {
	return setup(
		'dpkg_status' =>
			generate_n_installed_packages(@_) .
			entail(compose_installed_record('h', 1, 'on-hold'=>1)),
		'packages' => [
			compose_package_record('cc', 8) . "Depends: h (= 2)\n" ,
			compose_package_record('cc', 4) . "Breaks: p\n" ,
			compose_package_record('h', 2) ,
		],
	);
}

sub test {
	my ($count, $upgrade_h_expected) = @_;

	my $cupt = setup_cupt($count);
	my $offer = get_first_offer("$cupt install --select=flexible cc");

	my $expected_cc_version = $upgrade_h_expected ? 8 : 4;

	my $comment = "breaks $count packages, upgrade of h expected: $upgrade_h_expected";
	is(get_offered_version($offer, 'cc'), $expected_cc_version, $comment) or diag($offer);
}

test(0 => 0);
test(5 => 0);
test(20 => 0);
test(100 => 0);
test(200 => 0);
test(500 => 0);
test(1000 => 1);
test(2000 => 1);
test(5000 => 1);

