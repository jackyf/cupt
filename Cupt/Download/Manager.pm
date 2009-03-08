package Cupt::Download::Manager

use 5.10.0;
use strict;
use warnings;

use fields qw(_active_queries _waiting_queries);

use Cupt::Core;

=head1 METHODS

=head2 new

creates new Cupt::Download::Manager and returns reference to it

=cut

sub new {
	my $class = shift;
	my $self = fields::new($class);
	$self->{_active_queries} = [];
	$self->{_waiting_queries} = [];
}

=head2 add

method, adds download query to queue

Parameters:

I<uri> - URI to download

I<filename> - target filename

=cut

sub add ($$$) : locked method {
	my ($self, $uri, $filename) = @_;

	push $self->{_waiting_queries};
}

