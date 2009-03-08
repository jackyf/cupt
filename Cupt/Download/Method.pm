package Cupt::Download::Method

=head1 ABSTRACT

Base class of all cupt downloading methods. It should be never instantiated
directly. If you want to wrote your own download method for Cupt, you are need
to implement all methods specified under.

=head1 METHODS

=head2 new

return the reference to Cupt::Download::Method.

Parameters:

I<config> - reference to Cupt::Config

I<uri> - string that determines which URL to download

I<filename> - target file name

=cut

sub new {
	my $class = shift;
	return bless {} => $class;
}

=head2 perform

downloads specified file

=cut

sub perform {
	# stub
}

