use Test::More tests => 12;

require(get_rinclude_path('common'));

sub setup_cupt {
	return setup(
		'dpkg_status' =>
			generate_n_installed_packages(@_) .
			entail(compose_installed_record('up', '4')),
		'packages' => [
			compose_package_record('up', '5') ,
			compose_package_record('bb', 7) . "Depends: up (= 5)\n" ,
			compose_package_record('bb', 8) . "Breaks: p\n" ,
		],
	);
}

sub test {
	my ($score, $count, $upgrade_expected) = @_;

	my $cupt = setup_cupt($count);

	my $upgrade_option = defined $score ? "-o cupt::resolver::score::upgrade=$score" : '';
	my $offer = get_first_offer("$cupt install --select=flexible bb $upgrade_option");

	my $score_comment = ($score // 'default');
	my $comment = "upgrade score: $score_comment, breaks $count packages, upgrade expected: $upgrade_expected";

	my $expected_bb_version = $upgrade_expected ? 7 : 8;
	is(get_offered_version($offer, 'bb'), $expected_bb_version, $comment) or diag($offer);
}

test(undef, 0 => 0);
test(undef, 1 => 1);

test(200, 0 => 0);
test(200, 1 => 1);

test(-500, 0 => 0);
test(-500, 1 => 1);

test(-10000, 1 => 0);
test(-10000, 2 => 0);
test(-10000, 4 => 0);
test(-10000, 8 => 1);
test(-10000, 15 => 1);
test(-10000, 40 => 1);

