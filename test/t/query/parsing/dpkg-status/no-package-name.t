use Test::More tests => 9;

my $sample_record = compose_installed_record('def', 2);

sub test {
	my ($line) = @_;

	my $record = $sample_record;
	$record =~ s/.*?\n/$line\n/;

	my $cupt = setup('dpkg_status' => entail($record));

	$line =~ s/\n/{newline}/g;
	like(stdall("$cupt show def"), qr/^E: no package name in the record$/m, "line: '$line'")
			or diag($record);
}

test('Packge: def');
test('Package: ');
test('Package:');
test('P: def');
test('Q: def');
test('package: def');
test('PaCKage: def');
test("\n");
test('');

