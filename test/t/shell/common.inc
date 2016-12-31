package CuptShell;

use strict;
use warnings;

use Expect::Simple;

sub new {
	my ($class, $cupt) = @_;
	my $o = {};
	$o->{_impl} = Expect::Simple->new({
		'Cmd' => "$cupt shell",
		'Prompt' => "cupt> ",
		'DisconnectCmd' => 'q',
	});
	$o->{_error} = '';
	return bless($o, $class);
}

sub execute {
	my ($self, $text) = @_;

	if ($self->{_error}) {
		return 'Previously: ' . $self->{_error};
	}

	my $result;
	eval {
		$self->{_impl}->send($text);
	};
	if ($@) {
		$self->{_error} = $self->{_impl}->before() . "\n\n" . $@;
		return $self->{_error};
	}

	$result = $self->{_impl}->before();
	$result =~ s/\r//g;
	$result =~ s/^\Q$text\E\n//;
	return $result;
}

1;


package main;

use strict;
use warnings;

sub get_shell {
	my $cupt = shift;
	return CuptShell->new($cupt);
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

