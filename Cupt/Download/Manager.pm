package Cupt::Download::Manager

use 5.10.0;
use strict;
use warnings;
use threads;

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

=head2 download

method, adds group of download queries to queue. Blocks execution of program until
all downloads are done.

Parameters:

Sequence of pairs of:

I<uri> - URI to download

I<filename> - target filename

Example:

  my $download_manager = new Cupt::Download::Manager;
  $download_manager->download(
      'http://www.en.debian.org' => '/tmp/en.html',
	  'http://www.ru.debian.org' => '/tmp/ru.html',
	  'http://www.ua.debian.org' => '/tmp/ua.html'
  );

=cut

sub download ($@) {
	my $self = shift;

	my @threads;
	# schedule download of each uri at its own thread
	while (scalar @_) {
		# extract next pair
		my $uri = shift;
		my $filename = shift;

		threads->create(\&_download, $self, $uri, $filename);
	}
}
