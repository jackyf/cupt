sub save_snapshot {
	my $name = shift;

	subtest "saving snapshot $name" => sub {
		my $output = stdall("$cupt snapshot save $name");
		is($?, 0, "positive exit code")
				or diag($output);
		unlike($output, qr/^E: /m, "no errors");
	};
}

my $list_command = "$cupt snapshot list";

sub test_snapshot_list {
	my ($expected_output, $description) = @_;

	my $output = stdall($list_command);
	is($output, $expected_output, $description);
}

