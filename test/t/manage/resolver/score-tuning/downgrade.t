use Test::More tests => 12;

require(get_rinclude_path('common'));

sub setup_cupt {
	return setup(
		'dpkg_status' =>
			generate_n_installed_packages(@_) .
			entail(compose_installed_record('down', '1.0')),
		'packages' => [
			compose_package_record('down', '0.9') ,
			compose_package_record('bb', 6) . "Depends: down (<< 1)\n" ,
			compose_package_record('bb', 3) . "Breaks: p\n" ,
		],
	);
}

sub test {
	my ($downgrade_score, $count, $downgrade_expected) = @_;

	my $cupt = setup_cupt($count);

	my $downgrade_option = defined $downgrade_score ? "-o cupt::resolver::score::downgrade=$downgrade_score" : '';
	my $offer = get_first_offer("$cupt install --select=flexible bb $downgrade_option");

	my $score_comment = ($downgrade_score // 'default');
	my $comment = "downgrade score: $score_comment, breaks $count packages, downgrade expected: $downgrade_expected";

	my $expected_bb_version = $downgrade_expected ? 6 : 3;
	is(get_offered_version($offer, 'bb'), $expected_bb_version, $comment) or diag($offer);
}

test(undef, 0 => 0);
test(undef, 1 => 0);
test(undef, 2 => 0);
test(undef, 3 => 0);
test(undef, 5 => 0);
test(undef, 8 => 1);
test(undef, 13 => 1);
test(undef, 20 => 1);
test(undef, 40 => 1);

test(+8000, 1 => 0);
test(+12000, 1 => 1);
test(-100000, 40 => 0);

