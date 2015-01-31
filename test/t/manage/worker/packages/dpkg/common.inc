eval get_inc_code('../common');
die($@) if $@;

sub test_dpkg_sequence {
	my ($user_command, @expected) = @_;

	my $comment = $user_command;
	$user_command =~ s/#.*//;

	my $output = stdall(get_worker_command($cupt, $user_command));

	my @parsed_output = parse_dpkg_commands($output);
	is_deeply(\@parsed_output, \@expected, $comment) or
			diag($output);
}
