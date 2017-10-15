use Test::More tests => 4;

my $cupt;

sub lsetup {
	my ($latest_versions_available, $archive) = @_;

	my $packages = [];
	if ($latest_versions_available) {
		$packages = [
			compose_package_record('eip', '0'),
			compose_package_record('mip', '0'),
			compose_package_record('aip', '0'),
		];
	}

	$cupt = TestCupt::setup(
		'dpkg_status' =>
			entail(compose_installed_record('eip', '0') . "Essential: yes\n") .
			entail(compose_installed_record('mip', '0')) .
			entail(compose_installed_record('aip', '0')) ,
		'extended_states' =>
			entail(compose_autoinstalled_record('aip')) .
			entail(compose_autoinstalled_record('eip')),
		'releases' => [
			{
				'archive' => $archive,
				'packages' => $packages,
			},
		],
	);
}

sub compose_score_argument {
	my ($name, $value) = @_;
	return defined $value ? "-o cupt::resolver::score::$name=$value" : '';
}

sub get_printable_score {
	my ($value) = @_;
	return $value // 'default';
}

my $request_type;
my $default_release_enabled;

sub get_first_offer_for {
	my ($r_score, $ra_score, $re_score) = @_;
	
	my $score_arguments = 
			compose_score_argument('removal', $r_score) . ' ' .
			compose_score_argument('removal-of-autoinstalled', $ra_score) . ' ' .
			compose_score_argument('removal-of-essential', $re_score);

	my $cupt_options = "--no-auto-remove -o debug::resolver=yes";
	if ($default_release_enabled) {
		$cupt_options .= " -t tomorrow";
	}
	return get_first_offer("$cupt remove $request_type '*' $score_arguments $cupt_options"); 
}

sub eis {
	my ($offer, $package, $result) = @_;

	my $expected_version = $result ? get_unchanged_version() : get_empty_version();
	is(get_offered_version($offer, $package), $expected_version, $package) or
			diag(get_debug_part($offer));
}

sub get_debug_part {
	my ($input) = @_;
	return join("\n", grep { m/^D:/ } split("\n", $input));
}

sub test {
	my ($r_score, $ra_score, $re_score, $eip_result, $mip_result, $aip_result) = @_;
	my ($package, $score) = @_;

	my $r_score_printable = get_printable_score($r_score);
	my $ra_score_printable = get_printable_score($ra_score);
	my $re_score_printable = get_printable_score($re_score);

	my $comment = "request type: $request_type, " .
			"scores: r=$r_score_printable,ra=$ra_score_printable,re=$re_score_printable, " .
			"expected states: eip=$eip_result, mip=$mip_result, aip=$aip_result";

	my $offer = get_first_offer_for($r_score, $ra_score, $re_score);

	subtest $comment => sub {
		eis($offer, 'eip', $eip_result);
		eis($offer, 'mip', $mip_result);
		eis($offer, 'aip', $aip_result);
	};

}

sub test_group {
	my ($latest_versions_available, $archive) = @_;

	lsetup($latest_versions_available, $archive);

	my $group_comment = "latest: $latest_versions_available, default release enabled: $default_release_enabled, archive: $archive";

	subtest $group_comment => sub {
		$request_type = '--wish';
		test(undef, undef, undef, 1, 1, 0);

		$request_type = '--try';
		test(undef, undef, undef, 1, 0, 0);

		$request_type = '--must';
		test(undef, undef, undef, 0, 0, 0);
		test(-100_000_000, -100_000_100, -100_000_000, 0, 0, 0);

		$request_type = '--importance=0';
		test(undef, undef, undef, 1, 1, 1);
		test(500, undef, undef, 1, 0, 0);
		test(-1000, 1200, undef, 1, 1, 0);
		test(undef, undef, -200, 1, 1, 1);
		test(50, 50, -50, 0, 0, 0);
		test(undef, undef, 2000, 0, 1, 1);
		test(-500, undef, 2000, 0, 1, 0);

		test(-400, 20, 20, 1, 1, 1);
		test(-500, 700, -600, 1, 1, 0);
		test(-400, 700, -250, 0, 1, 0);
		test(500, 200, -1100, 1, 0, 0);
		test(100, -500, 0, 1, 0, 1);
		test(4000, 4000, -10000, 1, 0, 0);
		test(-2000, 1500, 5000, 0, 1, 1);
		test(100, -1000, 1200, 0, 0, 1);
		test(100, 200, 300, 0, 0, 0);
	};
}

foreach my $latest_versions_available (0, 1) {
	foreach (0, 1) {
		$default_release_enabled = $_;
		test_group($latest_versions_available, 'today');
	}
}

