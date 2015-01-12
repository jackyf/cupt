use TestCupt;
use Test::More tests => 4;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('aa', 1) . "Recommends: zz\n") .
		entail(compose_installed_record('bb', 1) . "Suggests: zz\n") .
		entail(compose_installed_record('zz', 555)),
	'extended_states' =>
		entail(compose_autoinstalled_record('zz')),
);

eval get_inc_code('common');

sub test {
	my ($recommends_is_on, $suggests_is_on, $expected_output) = @_;

	my $options = "-o cupt::resolver::keep-recommends=$recommends_is_on " .
			"-o cupt::resolver::keep-suggests=$suggests_is_on";

	test_why('zz', $options, $expected_output, "recommends: $recommends_is_on, suggests: $suggests_is_on");
}

test(0, 0, '');
my $recommends_output = "aa 1: Recommends: zz\n";
test(1, 0, $recommends_output);
test(1, 1, $recommends_output);
test(0, 1, "bb 1: Suggests: zz\n");

