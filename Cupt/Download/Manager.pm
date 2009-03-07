package Cupt::Download::Manager

use 5.10.0;
use strict;
use warnings;

use fields qw(_curl_share_handle);

use Cupt::Core;

use WWW::Curl::Share;

=head1 METHODS

=head2 new

creates new Cupt::Download::Manager and returns reference to it

=cut

sub new {
	my $class = shift;
	my $self = fields::new($class);

	$self->{_curl_share_handle} = new WWW::Curl::Share;
}

=head2

=cut

sub add ($$$) {
	my 
}

