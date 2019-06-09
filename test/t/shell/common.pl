use CuptInteractive;

use strict;
use warnings;

sub get_shell {
	my $cupt = shift;
	return CuptInteractive->new("$cupt shell");
}

sub test_output_identical_with_non_shell {
	my ($cupt, $cupt_shell, $command, $base_command) = @_;
	$base_command //= $command;

	my $output_normal = stdall("$cupt $base_command");
	is($?, 0, 'command succeeded')
			or diag($output_normal);
	my $output_shell = $cupt_shell->execute($command);
	is($output_shell, $output_normal, "comparing output");
}

1;

