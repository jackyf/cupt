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
	return bless($o, $class);
}

sub execute {
	my ($self, $text) = @_;

	$self->{_impl}->send($text);
	my $result = $self->{_impl}->before();

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

