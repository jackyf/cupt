use Test::More tests => 15*3;

my $status = 'Status: install ok installed';

sub compose_dummy_record {
	my ($package) = @_;
	return "Package: $package\n$status\n";
}

my $record_position;

sub get_records {
	my ($main_record) = @_;
	my @dummy_records = map { entail(compose_dummy_record("dum$_")) } (1..$record_position);
	return join("", @dummy_records) . $main_record;
}

sub test {
	my ($record, $expected_result) = @_;

	my $installed = get_records(entail("Package: ppp\n$record\n"));
	my $cupt = setup('dpkg_status' => $installed);
	my $output = stdall("$cupt pkgnames");

	$record =~ s/\n/{newline}/g;
	my $name = "dummy records: $record_position, record: '$record', expected result: $expected_result";

	subtest $name => sub {
		if ($expected_result == 2) {
			is($output, "ppp\n");
		} elsif ($expected_result) {
			is($output, '');
		} else {
			like($output, qr/^E: no status line in the record$/m);
		}
	} or diag($installed);
}

my $version = 'Version: 1';
my $uuu = 'Uuu: www';
my $prov = 'Provides: qqq';

sub test_group() {
	test("$status" => 1);
	test('' => 1);
	test("$version" => 0);
	test("$uuu" => 1);
	test("$prov" => 1);
	test("$status\n$version" => 2);
	test("$version\n$status" => 2);
	test("$version\n$status\n$uuu" => 2);
	test("$version\n$uuu" => 0);
	test("$uuu\n$version" => 0);
	test("$uuu\n$version\n$status" => 2);
	test("$version\n$uuu\n$status" => 2);
	test("$prov\n$status" => 1);
	test("$prov\n$version" => 0);
	test("$status\n$prov\n$uuu" => 1);
}

$record_position = 0;
test_group();
$record_position = 1;
test_group();
$record_position = 44;
test_group();

