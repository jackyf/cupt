use Test::More tests => 9;

my $cupt = TestCupt::setup(
	'dpkg_status' => [
		compose_installed_record('e', '1'),
		compose_installed_record('y', '1'),
		compose_installed_record('obs', '1'),
	],
	'releases' => [
		{
			'packages' => [
				compose_package_record('e', '1'),
				compose_package_record('rz', '30') . "Recommends: z\n",
				compose_package_record('z', '3'),
			],
		},
		{
			'archive' => 'x1y2',
			'packages' => [
				compose_package_record('y', '1'),
				compose_package_record('ne', '2') . "Breaks: e\n",
				compose_package_record('ny', '2') . "Breaks: y\n",
				compose_package_record('nobs', '2') . "Breaks: obs\n",
			],
		},
	],
	'extended_states' => [
		compose_autoinstalled_record('e'),
		compose_autoinstalled_record('y'),
		compose_autoinstalled_record('obs'),
	],
);

my $cupt_options = <<'END';
-o apt::default-release=x1y2
--no-auto-remove
END
$cupt_options =~ s/\n/ /g;

sub test_f {
	my ($importance, $expected_e_version, $expected_y_version) = @_;

	my $command = "--importance=$importance install ne ny nobs";
	subtest $command => sub {
		my $output = get_first_offer("$cupt $cupt_options $command");
		is(get_offered_version($output, 'e'), $expected_e_version, 'e') or diag($output);
		is(get_offered_version($output, 'y'), $expected_y_version, 'y') or diag($output);
		is(get_offered_version($output, 'obs'), get_empty_version(), 'obs') or diag($output);
	}
} 

sub test_z {
	my ($command, $expected_version) = @_;

	my $output = get_first_offer("$cupt $cupt_options $command");
	is(get_offered_version($output, 'z'), $expected_version, "z: $command") or diag($output);
}

test_f(330, get_empty_version(), get_empty_version());
test_f(230, get_empty_version(), get_unchanged_version());
test_f(130, get_unchanged_version(), get_unchanged_version());

test_z('--try install z', 3);
test_z('--wish install z', 3);
test_z('--try install rz', 3);
test_z('--wish install rz', 3);
test_z('--importance=50 install z', get_unchanged_version());
test_z('--importance=1 install z', get_unchanged_version());

