# https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=764238

use TestCupt;
use Test::More tests => 1;

use warnings;
use strict;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('pp', '1.2.3-4')),
	'preferences' =>
		compose_pin_record('pp', '1.2.3-4', 11782),
);

my $output = stdout("$cupt policy pp");

my ($result_priority) = ($output =~ m/\Q1.2.3-4\E.* (\d+)/m);
$result_priority //= '<undefined>';

TODO: {
	local $TODO = 'fix!';
	is($result_priority, 11782) or diag($output);
}

