use TestCupt;
use Test::More tests => 3;

use strict;
use warnings;

eval get_inc_code('common');

my $cupt = TestCupt::setup();

sub test {
	my ($option, $lowercase_present) = @_;

	my $value = 'good-value';
	my $output = stdall("$cupt config-dump -o $option=$value");

	subtest $option => sub {
		test_option($output, $option, $value);
		if ($lowercase_present) {
			test_option($output, lc($option), $value);
		}
	};
}

test('APT::Architecture', 1);
test('Apt::default-release', 1);
test('Aptitude::Some-Other::Option', 0);

