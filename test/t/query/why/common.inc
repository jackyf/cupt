sub test_why_raw {
	my ($package, $options, $main_check, $description) = @_;

	subtest $description => sub {
		my $command = "$cupt why $options $package";
		my $output = stdall($command);
		my $exit_code = $?;
		if ($output !~ m/^E: /) {
			is($exit_code, 0, 'operation succeeded');
		} else {
			isnt($exit_code, 0, 'operation failed');
		}
		$main_check->($output);
	};
}

sub test_why {
	my ($package, $options, $expected_output, $description) = @_;

	$expected_output =~ s/(\w+) (\d):/$1 $2^installed:/g;

	my $main_check = sub {
		my $output = shift;
		is($output, $expected_output, 'output is correct');
	};
	test_why_raw($package, $options, $main_check, $description);
}

sub test_why_regex {
	my ($package, $options, $regex, $description) = @_;

	my $main_check = sub {
		my $output = shift;
		like($output, $regex, 'output is correct');
	};
	test_why_raw($package, $options, $main_check, $description);
}

