package CuptInteractive;

use strict;
use warnings;

use Expect::Simple;

sub new {
	my ($class, $command, $prompt) = @_;
	my $o = {};
	$o->{_impl} = Expect::Simple->new({
		'Cmd' => $command,
		'Prompt' => $prompt//'cupt> ',
		'DisconnectCmd' => 'q',
		'Timeout' => '20',
	});
	$o->{_error} = '';
	my $self = bless($o, $class);

	$self->{_initial_output} = $self->_last_output();

	return $self;
}

sub initial_output {
	my $self = shift;
	return $self->{_initial_output};
}

sub execute {
	my ($self, $text) = @_;

	if ($self->{_error}) {
		return 'Previously: ' . $self->{_error};
	}

	eval {
		$self->{_impl}->send($text);
	};
	if ($@) {
		$self->{_error} = $self->{_impl}->before() . "\n\n" . $@;
		return $self->{_error};
	}

	my $result = $self->_last_output();
	$result =~ s/^\Q$text\E\n//;
	return $result;
}

sub _last_output {
	my $self = shift;

	my $result = $self->{_impl}->before();
	$result =~ s/\r//g;
	return $result;
}

1;

