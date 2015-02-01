sub test_snapshot_command {
	my ($arguments, $expected_error, $description) = @_;

	my $output;
	subtest $description => sub {
		$output = stdall("$cupt snapshot $arguments");
		my $exit_code = $?;

		if (defined $expected_error) {
			isnt($exit_code, 0, 'negative exit code');
			like($output, $expected_error, 'error message printed');
		} else {
			is($exit_code, 0, 'positive exit code');
			unlike($output, qr/^E: /m, "no errors");
		}
	} or diag($output);
}

sub save_snapshot {
	my $name = shift;
	test_snapshot_command("save $name", undef, "saving snapshot $name");
}

my $list_command = "$cupt snapshot list";

sub test_snapshot_list {
	my ($expected_output, $description) = @_;

	my $output = stdall($list_command);
	is($output, $expected_output, $description);
}

