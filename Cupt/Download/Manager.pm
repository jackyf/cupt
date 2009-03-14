package Cupt::Download::Manager;

use 5.10.0;
use strict;
use warnings;

use URI;
use IO::Handle;

use fields qw(_config _progress _downloads_done _worker_input_fh _worker_pid);

use Cupt::Core;
use Cupt::Download::Methods::Curl;

=head1 METHODS

=head2 new

creates new Cupt::Download::Manager and returns reference to it

Parameters:

I<config> - reference to Cupt::Config

I<progress> - reference to subclass of Cupt::Download::Progress

=cut

sub __fhbits {
	my($bits, @file_handles) = @_;
	for (@fhlist) {
		vec($bits,fileno($_),1) = 1;
	}
	return $bits;
}


sub new ($$$) {
	my $class = shift;
	my $self;
	$self = fields::new($class);
	$self->{_config} = shift;
	$self->{_progress} = shift;

	socketpair($self->{_main_fh}, $self->{_worker_fh}, AF_UNIX, SOCK_STREAM, PF_UNSPEC);
	$self->{_main_fh}->autoflush(1);
	$self->{_worker_fh}->autoflush(1);

	if (my $pid = fork()) {
		# this is a main process
		return $self;
	} else {
		# this is background worker process
		$pid // myinternaldie("unable to fork: $!");

		my %done_downloads;
		my %active_downloads;
		my @waiting_downloads;
		my $max_simultaneous_downloads_allowed = $self->{_config}->var('cupt::downloader::max-simultaneous-downloads');
		while (my @params = split(chr(0), readline($self->{_worker_fh}))) {
			my $command = shift @params;
			my $uri;
			my $filename;
			my $waiter_thread_queue;

			my $proceed_next_download = 0;
			given ($command) {
				when ('exit') { return; }
				when ('download') {
					# new query appeared
					($uri, $filename, $waiter_thread_queue) = @params;
					# check if this download was already done
					if (exists $done_downloads{$uri,$filename}) {
						# just end it
						$worker_queue->enqueue([ 'done', $uri, $filename, 1, undef ]);
					} elsif (scalar keys %active_downloads >= $max_simultaneous_downloads_allowed) {
						# put the query on hold
						push @waiting_downloads, [ $uri, $filename, $waiter_thread_queue ];
					} else {
						$proceed_next_download = 1;
					}
				}
				when ('done') {
					# some query ended
					($uri, $filename, my $result, my $error) = @params;
					# send an answer for a download
					$active_downloads{$uri,$filename}->enqueue([ $result, $error ]);

					# removing the query from active download list and put it to
					# the list of ended ones
					delete $active_downloads{$uri,$filename};
					$done_downloads{$uri,$filename} = 1;

					if (scalar @waiting_downloads) {
						# put next of waiting queries
						($uri, $filename, $waiter_thread_queue) = @{shift @waiting_downloads};
						$proceed_next_download = 1;
					}
				}
				when ('progress') {
					# progress meter needs updating
					$self->{_progress}->progress(@params);
				}
				default { myinternaldie("download manager: invalid worker command"); }
			}
			$proceed_next_download or next;
			# filling the active downloads hash
			$active_downloads{$uri,$filename} = $waiter_thread_queue;
			# there is a space for new download, start it
			(async {
				my ($result, $error) = $self->_download($uri, $filename);
				$worker_queue->enqueue([ 'done', $uri, $filename, $result, $error ]);
			})->detach();
		}
		close $self->{_main_fh};
		exit;
	}
}

sub DESTROY {
	my ($self) = @_;
	# shutdowning worker thread
	waitpid($self->{_worker_pid}, 0);
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

	my @waiter_queues;
	# schedule download of each uri at its own thread
	while (scalar @_) {
		# extract next pair
		my $uri = shift;
		my $filename = shift;
		print "manager::download: '$uri' -> '$filename'\n";

		# schedule new download

		my $waiter_queue = new Thread::Queue;
		my $worker_queue = $self->{_worker_queue};
		$worker_queue->enqueue([ 'download', $uri, $filename, $waiter_queue ]);
		push @waiter_queues, $waiter_queue;
	}

	# all are scheduled successfully, wait for them
	foreach my $waiter_queue (@waiter_queues) {
		my ($result, $error_string) = @{$waiter_queue->dequeue()};
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
	my $protocol = URI->new($uri)->scheme();
	my $handler_name = $protocol_handlers{$protocol} // 
			mydie("no protocol download handler defined for $protocol");

	my $handler;
	{
		no strict 'subs';
		# create handler by name
		$handler = "Cupt::Download::Methods::$handler_name"->new();
	}
	# download the file
	my $sub_callback = sub {
		$self->{_worker_queue}->enqueue([ 'progress', $uri, $filename, @_ ]);
	};
	$handler->perform($self->{_config}, $uri, $filename, $sub_callback); 
}

1;

