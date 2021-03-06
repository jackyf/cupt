use Test::More tests => 16;

require(get_rinclude_path('common'));

my $cupt = setup();

sub test {
	my ($from, $to, $value) = @_;

	my $output = stdall("$cupt config-dump -o $from=$value");

	subtest "$from=$value --> $to=$value" => sub {
		test_option($output, $from, $value);
		test_option($output, $to, $value);
	};
}

sub test_pair {
	test(@_, 'yes');
	test(@_, 'no');
}

sub test_group {
	my ($from, $to) = @_;
	test_pair($from, $to);
	test_pair(uc($from), $to);
}

test_group('apt::get::allowunauthenticated', 'cupt::console::allow-untrusted');
test_group('apt::get::assume-yes', 'cupt::console::assume-yes');
test_group('apt::get::automaticremove', 'cupt::resolver::auto-remove');
test_group('apt::get::purge', 'cupt::worker::purge');

