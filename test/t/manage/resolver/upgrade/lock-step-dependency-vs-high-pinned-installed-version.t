use Test::More tests => 48;

sub generate_lockstep_depender {
	my ($number, $version, $generator) = @_;
	return $generator->("p$number", $version) . "Depends: ip (= $version)\n";
}

sub generate_n_lockstep_dependers {
	my ($count, $version, $generator) = @_;
	return map { generate_lockstep_depender($_, $version, $generator) } (1..$count);
}

sub setup_cupt {
	my ($high_pin, $lockstep_count) = @_;

	return TestCupt::setup(
		'dpkg_status' => [
			compose_installed_record('ip', '1.6'),
			generate_n_lockstep_dependers($lockstep_count, '1.6', \&compose_installed_record),
		],
		'releases' => [
			{
				'archive' => 'tf',
				'packages' => [
					compose_package_record('ip', '1.8'),
					generate_n_lockstep_dependers($lockstep_count, '1.8', \&compose_package_record),
				],
			},
		],
		'preferences' =>
			compose_version_pin_record('ip', '1.6*', $high_pin),
	);
}

my $pin;
my $priority_downgrade_score;

sub test {
	my ($release_is_default, $lockstep_count, $ip_upgrade_expected) = @_;

	$lockstep_count = int($lockstep_count);
	my $expected_ip_version = ($ip_upgrade_expected ? '1.8' : get_unchanged_version());

	my $priority_downgrade_score_comment = 'priority downgrade score: ' . ($priority_downgrade_score // 'default');
	my $comment = "$priority_downgrade_score_comment, high pin: $pin, " .
			"release is default: $release_is_default, " .
			"lock-step count: $lockstep_count, upgrade expected: $ip_upgrade_expected";

	my $cupt = setup_cupt($pin, $lockstep_count);

	my $parameters = ($release_is_default ? '-t tf ' : '');
	if (defined $priority_downgrade_score) {
		$parameters .= "-o cupt::resolver::score::version-factor::priority-downgrade=$priority_downgrade_score";
	}
	my $offer = get_first_offer("$cupt $parameters safe-upgrade");

	subtest $comment => sub {
		like($offer, regex_offer, 'resolving succeeded');

		is(get_offered_version($offer, 'ip'), $expected_ip_version, 'ip');

		for (1..$lockstep_count) {
			my $package = "p$_";
			is(get_offered_version($offer, $package), $expected_ip_version, $package);
		}
	}
}

sub test_subgroup {
	my ($release_is_default, $lockstep_lower_bound, $lockstep_bigger_bound) = @_;

	my $ris = $release_is_default;

	test($ris, $lockstep_lower_bound / 4 => 0);
	test($ris, $lockstep_lower_bound / 2 => 0);
	test($ris, $lockstep_lower_bound / 1 => 0);
	test($ris, $lockstep_bigger_bound * 1 => 1);
	test($ris, $lockstep_bigger_bound * 2 => 1);
	test($ris, $lockstep_bigger_bound * 4 => 1);
}

sub test_group {
	test_subgroup(0, @_);
	test_subgroup(1, @_);
}

$pin = 1600;
test_group(2, 5);

$pin = 3000;
test_group(7, 12);

$pin = 8000;
test_group(22, 33);

$pin = 1000;
$priority_downgrade_score = 6000;
test_subgroup(0, 4, 8);

$priority_downgrade_score = 300000;
test_subgroup(1, 4, 8);

