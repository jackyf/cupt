use Test::More tests => 2;

eval get_inc_code('common');

sub test {
	my $strong_dependency = shift;

	my $dep = ($strong_dependency ? 'Depends' : 'Recommends');
	my $expected_options = ($strong_dependency ? ['--force-depends'] : []);

	my $cupt = setup(
		'dpkg_status' => [
			compose_installed_record('unchanged', 1) . "$dep: mta\n" ,
			compose_installed_record('exim4', 2) . "Provides: mta\nConflicts: mta\n" ,
		],
		'packages' => [
			compose_package_record('postfix', 3) . "Provides: mta\nConflicts: mta\n" ,
		],
	);

	test_dpkg_sequence($cupt, "install postfix # $dep",
			['--remove', $expected_options, ['exim4']],
			['--install', $expected_options, ['<postfix 3>']]);
}

test(0);
test(1);

