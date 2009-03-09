package Cupt::Download::Manager

use 5.10.0;
use strict;
use warnings;

use fields qw(_active_queries _waiting_queries _downloads_done);

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
	$self->{_downloads_done} = {};
}

=head2 add

method, adds download query to queue

Parameters:

I<uri> - URI to download

I<filename> - target filename

=cut

sub download ($$$) : locked method {
	my ($self, $uri, $filename) = @_;

	if 
	push $self->{_waiting_queries};
}

