package Cupt::Download::Method

=head1 ABSTRACT

Base package of all cupt downloading methods. It should be never instantiated
directly. If you want to wrote your own download method for Cupt, you are need
to implement 'pefrom' method specified under.

=head1 METHODS

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
  "dns" - "<hostname>";
  "connecting" - "<ip>";
  "expected-size" - "<size of file to download>";
  "downloading" - "<number of bytes done for download>" (in case of resumed
download this number should include already downloaded size).

=cut

sub perform ($$$$$) {
	# stub
}

