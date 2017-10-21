use Test::More tests => 5;

sub test {
	my ($line, $expected_is_important) = @_;
	my $cupt = setup('packages' => [ compose_package_record('pp', 0) . "$line\n" ]);

	my $output = stdout("$cupt show pp");
	my @found_important_values = ($output =~ m/^Important: (.*)$/mg);
	my @expected_important_values = map { 'yes' } (1..$expected_is_important);

	is_deeply(\@found_important_values, \@expected_important_values, $line);
}

test('VeryImportant: yes' => 0);
test('Important: yes' => 1);
test('Important: yes!' => 0);
test('Important: no' => 0);
test('Important: 781njdps' => 0);

