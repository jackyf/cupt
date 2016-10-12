use IPC::Run3;

sub check_exit_code {
	my ($command, $expected_success, $desc) = @_;
	my $checker = $expected_success ? \&is : \&isnt;

	my $output;
	run3($command, \undef, \$output, \$output);
	$checker->($?, 0, $desc);
	diag($output);
}

1;

