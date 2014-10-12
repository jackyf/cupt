use TestCupt;
use Test::More tests => 36;

use strict;
use warnings;

sub generate_lockstep_depender {
	my ($number, $version, $generator) = @_;
	return entail($generator->("p$number", $version) . "Depends: ip (= $version)\n");
}

sub generate_n_lockstep_dependers {
	my ($count, $version, $generator) = @_;
	return join('', map { generate_lockstep_depender($_, $version, $generator) } (1..$count));
}

sub setup_cupt {
	my ($high_pin, $lockstep_count) = @_;

	return TestCupt::setup(
		'dpkg_status' =>
			entail(compose_installed_record('ip', '1.6')) .
			generate_n_lockstep_dependers($lockstep_count, '1.6', \&compose_installed_record),
		'packages2' =>
			[
				[
					'archive' => 'tf',
					'content' =>
						entail(compose_package_record('ip', '1.8')) .
						generate_n_lockstep_dependers($lockstep_count, '1.8', \&compose_package_record),
				],
			],
		'preferences' =>
			compose_pin_record('ip', '1.6*', $high_pin),
	);
}

my $pin;
my $test_number = 0;
my @failing_test_numbers = (3, 9, 15, 21, 33);

sub test {
	my ($release_is_default, $lockstep_count, $ip_upgrade_expected) = @_;

	$lockstep_count = int($lockstep_count);
	my $expected_ip_version = ($ip_upgrade_expected ? '1.8' : get_unchanged_version());

	my $comment = "high pin: $pin, release is default: $release_is_default, " .
			"lock-step count: $lockstep_count, upgrade expected: $ip_upgrade_expected";

	my $cupt = setup_cupt($pin, $lockstep_count);

	my $parameters = ($release_is_default ? '-t tf' : '');
	my $offer = get_first_offer("$cupt -V $parameters safe-upgrade -o debug::resolver=yes");

	++$test_number;
	local $TODO = 'too small pin influence' if grep { $_ == $test_number } @failing_test_numbers;

	is(get_offered_version($offer, 'ip'), $expected_ip_version, $comment) or diag($offer);
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

$pin = 2000;
test_group(1, 2);

$pin = 7000;
test_group(4, 7);

$pin = 25000;
test_group(14, 27);

