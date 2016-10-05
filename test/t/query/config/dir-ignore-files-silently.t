use Test::More tests => 9+4+4+5;

require(get_rinclude_path('common'));

my $cupt = setup();
my $the_option = 'cupt::console::use-colors'; # any would do
my $default_value = 'no';
my $the_value = 'kippis';

sub test_half {
	my ($parts_dir, $filename, $expected_result) = @_;
	my $path = "$parts_dir/$filename";
	generate_file($path, "$the_option \"$the_value\";\n");
	my $expected_value = $expected_result ? $the_value : $default_value;
	my $output = stdall("$cupt config-dump");
	test_option($output, $the_option, $expected_value);
	unlink($path);
}

sub test {
	my @params = @_;
	subtest "$_[0]" => sub {
		test_half('etc/apt/apt.conf.d', @params);
		test_half('etc/cupt/cupt.conf.d', @params);
	}
}

test('abc' => 1);
test('xyz78' => 1);
test('78xyz' => 1);
test('abc.conf' => 1);
test('a_b' => 1);
test('a-b' => 1);
test('~' => 0);
test('abc~' => 0);
test('abc~5' => 1);

test('abc.disabled' => 0);
test('abc_disabled' => 1);
test('abc.disabledouch' => 1);
test('23q.disable' => 1);

test('xyz.bak' => 0);
test('xyzbak' => 1);
test('xyz.bak8' => 1);
test('bakbakbar' => 1);

test('qwe.dpkg-bak' => 0);
test('qwe.dpkg-m' => 0);
test('qwe.dpkg-uqoweqpw' => 0);
test('qwe.d-m' => 1);
test('qwe.dpkg' => 1);

