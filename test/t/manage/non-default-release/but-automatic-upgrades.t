use TestCupt;
use Test::More tests => 10;

use strict;
use warnings;

sub setup_cupt {
	my ($installed_version) = @_;

	return TestCupt::setup(
		'dpkg_status' =>
			entail(compose_installed_record('aa', $installed_version)),
		'packages2' =>
			[
				[
					'archive' => 'stable',
					'content' => entail(compose_package_record('aa', '1.0')),
				],
				[
					'archive' => 'stable-backports',
					'not-automatic' => 1,
					'but-automatic-upgrades' => 1,
					'content' => entail(compose_package_record('aa', '1.2')),
				],
				[
					'archive' => 'unstable',
					'content' => entail(compose_package_record('newp', 4) . "Recommends: aa (>= 1.1)\n"),
				],
				[
					'archive' => 'experimental',
					'not-automatic' => 1,
					'content' => entail(compose_package_record('aa', '2.0')),
				],
			],
		);
}

sub test {
	my ($installed_version, $command, $expected_version) = @_;

	my $cupt = setup_cupt($installed_version);
	my $offer = get_first_offer("$cupt $command -V -o debug::resolver=yes");

	my $comment = "installed version: $installed_version, command: $command, expected version: $expected_version";
	is(get_offered_version($offer, 'aa'), $expected_version, $comment) or diag($offer);
}


my $expl = 'improve score system';

test('0.9', 'install aa' => '1.0');
TODO: {
	local $TODO = $expl;
	test('0.9', 'install newp' => get_unchanged_version());
}
test('0.9', 'safe-upgrade' => '1.0');

TODO: {
	local $TODO = $expl;
	test('1.0', 'install --importance=34 aa=1.2' => get_unchanged_version());
}
test('1.0', 'install aa' => get_unchanged_version());
TODO: {
	local $TODO = $expl;
	test('1.0', 'install newp' => get_unchanged_version());
}
test('1.0', 'safe-upgrade' => get_unchanged_version());

TODO: {
	local $TODO = $expl;
	test('1.1~alpha3', 'install aa' => get_unchanged_version());
	test('1.1~alpha3', 'install newp' => get_unchanged_version());
}
test('1.1~alpha3', 'safe-upgrade' => '1.2');

