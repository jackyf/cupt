use TestCupt;
use Test::More tests => 18;

use strict;
use warnings;

my $sha = 0;

sub compose_ttt_record {
	my ($is_valid) = @_;
	return entail(compose_package_record('ttt', 4, 'sha' => ++$sha) . ($is_valid ? '' : "Depends: broken\n"));
}

sub setup_cupt {
	my $ttts = join('', map { compose_ttt_record($_) } @_);
	return TestCupt::setup(
		'dpkg_status' =>
			entail(compose_installed_record('ttt', 3)),
		'packages' =>
			entail(compose_package_record('nnn', 1) . "Depends: ttt (>= 4)\n") .
			$ttts ,
	);
}

my $command;

sub test {
	my ($valid1, $valid2, $valid3, $expected_suffix) = @_;

	my $cupt = setup_cupt($valid1, $valid2, $valid3);

	my $output = get_first_offer("$cupt $command");

	my $comment = "command: $command, 1st valid: $valid1, 2nd valid: $valid2, 3rd valid: $valid3, expected suffix: '$expected_suffix'";
	is(get_offered_version($output, 'ttt'), "4$expected_suffix", $comment)
			or diag($output);
}

sub test_group {
	test(1, 1, 1, '');
	test(1, 0, 1, '');
	test(1, 0, 0, '');

	test(0, 1, 1, '^dhs0');
	test(0, 1, 0, '^dhs0');

	test(0, 0, 1, '^dhs1');
}

$command = 'install --sf "version(4.*)"';
test_group();

$command = 'full-upgrade';
test_group();

$command = 'install nnn';
test_group();

