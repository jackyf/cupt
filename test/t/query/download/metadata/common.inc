use IPC::Run3;
use Test::Dir;

sub check_exit_code {
	my ($command, $expected_success, $desc) = @_;
	my $checker = $expected_success ? \&is : \&isnt;

	my $output;
	run3($command, \undef, \$output, \$output);
	$checker->($?, 0, $desc) or diag($output);
}

sub check_no_partial_files {
	my $partial_dir = 'var/lib/cupt/lists/partial';
	dir_exists_ok($partial_dir);
	dir_empty_ok($partial_dir);
}

1;

