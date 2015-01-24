use TestCupt;
use Test::More tests => 1;

use strict;
use warnings;

my $cupt;
eval get_inc_code('common');

$cupt = setup_for_worker(
	'dpkg_status' =>
		entail(compose_installed_record('aa', 1)) .
		entail(compose_installed_record('bb', 2)) ,
	'packages' =>
		entail(compose_package_record('bb', 3)) .
		entail(compose_package_record('cc', 4)) ,
);

test_dpkg_sequence('remove aa --install bb cc # action type priority',
		['--install', [], ['<bb 3>']],
		['--install', [], ['<cc 4>']],
		['--remove', [], ['aa']]);

