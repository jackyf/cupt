use TestCupt;
use Test::More tests => 5;

use strict;
use warnings;

eval get_inc_code('common');

my $cupt = TestCupt::setup();

sub test {
	my ($option, $initial_present, $lowercase_present) = @_;

	my $value = 'good-value';
	my $output = stdall("$cupt config-dump -o $option=$value");

	subtest $option => sub {
		if ($initial_present) {
			test_option($output, $option, $value);
		}
		if ($lowercase_present) {
			test_option($output, lc($option), $value);
		}
	};
}

test('APT::Architecture', 1, 1);
test('Apt::default-release', 1, 1);
test('Aptitude::Some-Other::Option', 1, 0);

test('Cupt::WoRkEr::PURGE', 0, 1);
test('CUPT::Cache::pin::addendums::hOLD', 0, 1);

