use Test::More tests => 5;

require(get_rinclude_path('common'));

my $cupt = setup();

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

