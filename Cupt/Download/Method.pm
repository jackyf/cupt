package Cupt::Download::Method;

=head1 NAME

Cupt::Download::Method - base class for all Cupt download methods

=head1 ABSTRACT

It should be never instantiated directly. If you want to write your own
download method for Cupt, you are need to implement 'perform' method specified
under.

=head1 METHODS

=head2 new

creates new Cupt::Download::Method object

=cut

sub new {
	my $class = shift;
	return bless {} => $class;
}

=head2 perform

downloads specified file

Parameters:

I<config> - reference to Cupt::Config

I<uri> - string that determines which URL to download

I<filename> - target file name

I<sub_callback> - subroutine to report status change of download, takes two arguments: I<name> and I<value>.
Allowed pairs I<name> - I<value>:
  "connecting" - "<ip>";
  "expected-size" - "<size of file to download>";
  "downloading" - "<number of bytes done for download>" (in case of resumed
download this number should include already downloaded size).

Returns: 0 if all went smoothly, error string in case of error

=cut

sub perform ($$$$$) {
	# stub
}

1;

