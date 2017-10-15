use Test::More tests => 7;

sub setup_cupt {
	my ($installed_version) = @_;

	my $installed = defined $installed_version ?
			[ compose_installed_record('aa', $installed_version) ] : [];

	return setup(
		'dpkg_status' => $installed,
		'releases' => [
			{
				'archive' => 'stable',
				'packages' => [
					compose_package_record('aa', '1.0'),
					compose_package_record('uu', '0'),
				],
			},
			{
				'archive' => 'stable-backports',
				'not-automatic' => 1,
				'but-automatic-upgrades' => 1,
				'packages' => [ compose_package_record('aa', '1.2') ],
			},
			{
				'archive' => 'unstable',
				'packages' => [ compose_package_record('newp', 4) . "Depends: aa (>= 1.1) | uu\n" ],
			},
			{
				'archive' => 'experimental',
				'not-automatic' => 1,
				'packages' => [ compose_package_record('aa', '2.0') ],
			},
		],
		'preferences' => compose_version_pin_record('uu', '*', 300),
	);
}

sub test {
	my ($installed_version, $command, $expected_version) = @_;

	my $cupt = setup_cupt($installed_version);
	my $offer = get_first_offer("$cupt $command -o debug::resolver=yes");

	my $iv_comment = ($installed_version // '<none>');
	my $comment = "installed version: $iv_comment, command: $command, expected version: $expected_version";
	is(get_offered_version($offer, 'aa'), $expected_version, $comment) or diag($offer);
}


my $expl = 'improve score system';

test(undef, 'install newp' => get_unchanged_version());

test('0.9', 'install aa' => '1.0');
test('0.9', 'safe-upgrade' => '1.0');

test('1.0', 'install aa' => get_unchanged_version());
test('1.0', 'safe-upgrade' => get_unchanged_version());

test('1.1~alpha3', 'install aa' => '1.2');
test('1.1~alpha3', 'safe-upgrade' => '1.2');

