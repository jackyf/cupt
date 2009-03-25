package Cupt::Download::Progress;

=head1 ABSTRACT

Base class for possible download progess meters

=cut

sub new {
	my $class = shift;
	my $self = {};
	$self->{_long_aliases} = {};
	$self->{_short_aliases} = {};
	$self->{_total_estimated_size} = undef;
    return bless $self => $class;
}

=head2 set_long_alias_for_uri

method, sets long alias for uri to show

=cut

sub set_long_alias_for_uri ($$$) {
	my ($self, $uri, $alias) = @_;
	$self->{_long_aliases}->{$uri} = $alias;
}

=head2 set_short_alias_for_uri

method, sets short alias for uri to show

=cut

sub set_short_alias_for_uri ($$$) {
	my ($self, $uri, $alias) = @_;
	$self->{_short_aliases}->{$uri} = $alias;
}

=head2 set_total_estimated_size

method, set estimated total size of downloads

Parameters:

I<total_size> - total estimated size in bytes

=cut

sub set_total_estimated_size ($$) {
	my ($self, $size) = @_;
	$self->{_total_estimated_size} = $size;
}

=head2 progress

this method is called everytime something changed within downloading process

Parameters:

I<uri> - URI of the download

Next parameters are the same as specified for the callback function for the
'perform' method of the Cupt::Download::Method class, consult its
documentation.

One exception:

I<start> - message turns download start

I<size> - size in bytes of the download, can be undef if it's unknown before
the download

=cut

sub progress ($$;@){
	# stub
}

=head2 finish

this method is called when all downloads are done

=cut

sub finish ($) {
	# stub
}

1;

