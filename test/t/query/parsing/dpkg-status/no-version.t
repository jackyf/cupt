use Test::More tests => 10;

my $status = 'Status: install ok installed';

sub test {
	my ($lines, $result) = @_;
	my $record = "Package: ppp\n$status\n$lines\n";
	my $cupt = setup('dpkg_status' => entail($record));

	$lines =~ s/\n/{newline}/g;
	my $expected_output = $result ? "ppp\n" : '';

	my $output = stdall("$cupt pkgnames");
	is($output, $expected_output, $lines);
}

test('Version: 11' => 1);
test('Version: 118888888888888888888888888888888888' => 1);
test('' => 0);
test('Provides: abc' => 0);
test("Version: 11\nProvides: abc" => 1);
test("Provides: abc\nVersion: 11" => 1);
test("SomeTag: some-value\nVersion: 11" => 1);
test("Version: 11\nDepends: qwe" => 1);
test("Enhances: bbb\nVersion: 11" => 1);
test("UUU: yyy\nZzz: www\nVersion: 125\nNnn: ooo" => 1);

