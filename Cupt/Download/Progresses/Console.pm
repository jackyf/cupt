package Cupt::Download::Progresses::Console;

use base Cupt::Download::Progress;

sub new {
	my $class = shift;
	my $self = $class->new();
}

sub progress {
	my ($self, @params) = @_;

	print "@params\n";
}

1;

