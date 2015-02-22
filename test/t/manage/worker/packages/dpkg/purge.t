use TestCupt;
use Test::More tests => 5;

use strict;
use warnings;

my $cupt;
eval get_inc_code('common');

$cupt = setup_for_worker(
	'dpkg_status' =>
		entail(compose_installed_record('aa', 1)) .
		entail(compose_removed_record('bb')) ,
);

test_dpkg_sequence('remove aa # removing installed package',
		['--remove', [], ['aa']]);
test_dpkg_sequence('remove bb # trying to remove config-files package');
test_dpkg_sequence('purge aa # purging installed package',
		['--purge', [], ['aa']]);
test_dpkg_sequence('purge bb # purging config-files package',
		['--purge', [], ['bb']]);
test_dpkg_sequence('remove aa --purge bb # removing and purging different packages',
		['--remove', [], ['aa']],
		['--purge', [], ['bb']]);

