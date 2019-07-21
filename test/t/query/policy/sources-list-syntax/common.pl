use IPC::Run3;

use strict;
use warnings;

sub run_case_raw {
	my ($desc, $hook, $success, $output_regex, $error_regex) = @_;

	my $source_list_lines = "";
	my $wrapped_hook = sub {
		my $result = $hook->($_[0]);
		$source_list_lines .= $result;
		return $result;
	};

	my $re = {'archive' => 'bozon', 'sources' => [], 'line-hook' => $wrapped_hook};
	my $cupt = setup('releases' => [$re]);
	run3("$cupt policysrc", \undef, \my $stdout, \my $stderr);
	my $exitcode = $?;

	my $orig_stdall = $stdout . $stderr;
	$stdout =~ s/^Source files:$//m; # only keep release lines
	$stderr =~ s/^.*signature.*$//mg; # ignore a warning no longer suppressed by the default option

	subtest $desc => sub {
		if ($success) {
			is($exitcode, 0, 'success');
		} else {
			isnt($exitcode, '0', 'failure');
		}

		my $presence_regex = qr/bozon/;
		if (defined($output_regex)) {
			like($stdout, $presence_regex, 'output line is present');
			like($stdout, $output_regex, 'output line is correct');
		} else {
			unlike($stdout, $presence_regex, 'output line is not present');
		}

		if (defined($error_regex)) {
			like($stderr, $error_regex, 'errors/warnings');
		} else {
			like($stderr, qr/\n*/, 'no errors/warnings');
		}
	} or diag("In:\n\n$source_list_lines\n\nOut:\n\n$orig_stdall");
}

1;

