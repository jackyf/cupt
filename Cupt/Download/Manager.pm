package Cupt::Download::Manager

use 5.10.0;
use strict;
use warnings;

use threads;
use threads::shared;
use Thread::Queue;

use fields qw(_downloads_done _worker_queue _worker_thread);

use Cupt::Core;

=head1 METHODS

=head2 new

creates new Cupt::Download::Manager and returns reference to it

Parameters:

I<config> - reference to Cupt::Config

=cut

sub new {
	my $class = shift;
	my $self : shared = fields::new($class);
	$self->{_config} = shift;
	$self->{_downloads_done} = {};
	$self->{_worker_queue} = new Thread::Queue;
	$self->{_worker_thread} = threads->create(\&_worker, $self);
}

sub _worker {
	my ($self) = @_;

	my %active_downloads;
	my @waiting_downloads;
	my $max_simultaneous_downloads_allowed = $self->{_config}->var('cupt::downloader::max-simultaneous-downloads');
	while (my @params = $self->{_worker_queue}->dequeue()) {
		my $command = shift @params;
		given ($command) {
			when ('exit') { return; }
			when ('download') {
				# new query appeared
				my ($uri, $filename, $waiter_thread_queue) = @params;
				if (scalar @active_downloads < $max_simultaneous_downloads_allowed) {
					# filling the active downloads hash
					$waiting_downloads{$uri,$filename}->{waiter_thread_queue} = $waiter_thread_queue;
					# there is a space for new download, start it
					async {
						my $worker_waiting_thread = new Thread::Queue;
						my ($result, $error_code) = $self->_download($uri, $filename);
						$worker_waiting_thread->enqueue('done', $uri, $filename);
					}
				} else {
					# put the query on hold
					push @waiting_downloads, [ $uri, $filename, $waiter_thread ];
				}
			}
			when ('done') {
				# some query ended
				my ($uri, $filename) = @params;
				delete $active_downloads{$uri,$filename};
			}
			default { myinternaldie("download manager: invalid worker command"); }
		}
	}
}

sub DESTROY {
	my ($self) = @_;
	# shutdowning worker thread
	$self->{_worker_queue}->enqueue('exit');
	$self->{_worker_thread}->join();
}

=head2 download

method, adds group of download queries to queue. Blocks execution of program until
all downloads are done.

Parameters:

Sequence of pairs of:

I<uri> - URI to download

I<filename> - target filename

Returns:

Pair of

I<return_code> - boolean, non-zero is success, zero is fail

I<fail_reason> - if I<return_code> is fail, this string determines the reason,
otherwise undef

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

	my @waiter_thread_queues;
	# schedule download of each uri at its own thread
	while (scalar @_) {
		# extract next pair
		my $uri = shift;
		my $filename = shift;

		if ($self->{_downloads_done}->{$uri,$filename}) {
			# hm, we did it already, skip it
			next;
		} else {
			# schedule new download

			my $waiter_thread_queue = new Thread::Queue;
			$self->{_worker_queue}->enqueue('download', $uri, $filename, $waiter_thread_queue);
			push @waiter_thread_queues, $waiter_thread_queue;
		}
	}

	# all are scheduled successfully, wait for them
	foreach my $waiter_thread_queue (@thread_waiters) {
		my ($result, $error_string) = $waiter_thread_queue->dequeue();
		if (!$result) {
			# this download has'n been processed smoothly
			return ($result, $error_string);
		}
	}

	# correct finish
	return (1, undef);
}

sub _download ($$$) {
	my ($self, $uri, $filename) = @_;

	my %protocol_handlers = (
		'http' => 'Curl',
		'ftp' => 'Curl',
	);
	my ($protocol) = ($uri =~ m{(\w+)::/});
	my $handler_name = $protocol_handlers{$protocol} // 
			mydie("no protocol download handler defined for $protocol");

	my $handler;
	{
		no strict 'subs';
		# create handler by name
		$handler = "Cupt::Download::Methods::$handler_name"->new();
	}
	# download the file
	$handler->perform($self->{_config}, $uri, $filename); 
}
