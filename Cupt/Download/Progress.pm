package Cupt::Download::Progress;

=head1 ABSTRACT

Base class for possible download progess meters

=cut

sub new {
	my $class = shift;
	my $self = {};
    return bless $self => $class;
}

sub progress {

}

1;

