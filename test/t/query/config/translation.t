use TestCupt;
use Test::More tests => 8;

use strict;
use warnings;

eval get_inc_code('common');

my $cupt = TestCupt::setup();

sub test {
	my ($from, $to, $value) = @_;

	my $output = stdall("$cupt config-dump -o $from=$value");

	subtest "$from=$value --> $to=$value" => sub {
		test_option($output, $from, $value);
		test_option($output, $to, $value);
	};
}

sub test_group {
	test(@_, 'yes');
	test(@_, 'no');
}

test_group('apt::get::allowunauthenticated', 'cupt::console::allow-untrusted');
test_group('apt::get::assume-yes', 'cupt::console::assume-yes');
test_group('apt::get::automaticremove', 'cupt::resolver::auto-remove');
test_group('apt::get::purge', 'cupt::worker::purge');

