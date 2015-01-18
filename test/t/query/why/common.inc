sub test_why {
	my ($package, $options, $expected_output, $description) = @_;
	$expected_output =~ s/(\w+) (\d):/$1 $2^installed:/g;

	subtest $description => sub {
		my $output = stdall("$cupt why $options $package");
		is($?, 0, 'operation succeeded');
		is($output, $expected_output, 'output is correct');
	}
}

