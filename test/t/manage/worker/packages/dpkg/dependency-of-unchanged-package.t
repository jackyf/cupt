use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $cupt;
eval get_inc_code('common');

sub test {
	my $strong_dependency = shift;

	my $dep = ($strong_dependency ? 'Depends' : 'Recommends');
	my $expected_options = ($strong_dependency ? ['--force-depends'] : []);

	$cupt = setup(
		'dpkg_status' =>
			entail(compose_installed_record('unchanged', 1) . "$dep: mta\n") .
			entail(compose_installed_record('exim4', 2) . "Provides: mta\nConflicts: mta\n"),
		'packages' =>
			entail(compose_package_record('postfix', 3) . "Provides: mta\nConflicts: mta\n"),
	);

	test_dpkg_sequence("install postfix # $dep",
			['--remove', $expected_options, ['exim4']],
			['--install', $expected_options, ['<postfix 3>']]);
}

test(0);
test(1);

