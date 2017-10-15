use Test::More tests => 4;
use CuptInteractive;

sub setup_with {
	my ($cupt_installed_version, $dpkg_installed_version) = @_;

	return setup(
		'dpkg_status' => [
			compose_installed_record('cupt', $cupt_installed_version) ,
			compose_installed_record('dpkg', $dpkg_installed_version) ,
			compose_installed_record('libvoo', '0.5') ,
			compose_installed_record('qqq', '1.4.4') ,
		],
		'packages' => [
			compose_package_record('cupt', '3.0') ,
			compose_package_record('dpkg', '1.50') ,
			compose_package_record('libvoo', '0.6') ,
			compose_package_record('voo', '0.6') ,
			compose_package_record('qqq', '1.4.4') ,
		],
	);
}

sub test {
	my ($text, $installed_versions, $expected_tools_upgrade_versions) = @_;
	my $cupt = setup_with(@$installed_versions);

	subtest $text => sub {
		my $process = CuptInteractive->new("$cupt -s dist-upgrade", "Do you want to continue? [y/N/q/a/rc/?]");
		my $output = $process->initial_output();
		if (scalar(keys %$expected_tools_upgrade_versions) > 0) {
			my $first_stage_versions = get_offered_versions($output);
			is_deeply($first_stage_versions, $expected_tools_upgrade_versions, 'first stage') or diag($output);
			$cupt = setup_with('3.0', '1.50');
			$output = $process->execute('y');
		}
		my $second_stage_versions = get_offered_versions($output);
		is_deeply($second_stage_versions, {'libvoo' => '0.6'}, 'second stage') or diag($output);
	};
}

test('up-to-date', ['3.0', '1.50'], {});
test('newer cupt', ['2.15', '1.50'], {'cupt' => '3.0'});
test('newer dpkg', ['3.0', '1.48'], {'dpkg' => '1.50'});
test('newer both', ['2.11', '1.43'], {'dpkg' => '1.50', 'cupt' => '3.0'});

