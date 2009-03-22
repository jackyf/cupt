package Cupt::Download::Manager;

use 5.10.0;
use strict;
use warnings;

use URI;
use IO::Handle;
use IO::Select;
use Fcntl qw(:flock);
use File::Temp;
use POSIX;

use fields qw(_config _progress _downloads_done _worker_fh _worker_pid _fifo_dir);

use Cupt::Core;
use Cupt::Download::Methods::Curl;

=head1 METHODS

=head2 new

creates new Cupt::Download::Manager and returns reference to it

Parameters:

I<config> - reference to Cupt::Config

I<progress> - reference to subclass of Cupt::Download::Progress

=cut

sub __my_write_pipe ($@) {
	my $fh = shift;
	my $string = join(chr(0), @_);
	my $len = length($string);
	my $packed_len = pack("S", $len);
	syswrite $fh, ($packed_len . $string);
}

sub __my_read_pipe ($) {
	my $fh = shift;
	my $packed_len;
	sysread $fh, $packed_len, 2;
	my ($len) = unpack("S", $packed_len);
	$len or mydie("attempt to read from closed pipe");
	my $string;
	sysread $fh, $string, $len;
	return split(chr(0), $string);
}

sub new ($$$) {
	my $class = shift;
	my $self;
	$self = fields::new($class);
	$self->{_config} = shift;
	$self->{_progress} = shift;

	# making fifo storage dir if it's absend
	$self->{_fifo_dir} = File::Temp::tempdir('cupt-XXXXXX', CLEANUP => 1, TMPDIR => 1) or
			mydie("unable to create temporary directory for fifo storage: $!");

	my $worker_fh;
	my $pid;
	do {
		# don't close this handle on forks
		local $^F = 10_000;

		$pid = open($worker_fh, "|-");
		defined $pid or
				myinternaldie("unable to create download worker stream: $!");
	};
	autoflush $worker_fh;
	$self->{_worker_fh} = $worker_fh;

	if ($pid) {
		# this is a main process
		$self->{_worker_pid} = $pid;
		return $self;
	} else {
		# this is background worker process

		# { ($uri, $filename) => 1 }
		my %done_downloads;
		# { ($uri, $filename) => $waiter_fh, $pid, $input_fh }
		my %active_downloads;
		# [ $uri, $filename, $filehandle ]
		my @waiting_downloads;
		# { $uri => size }
		my %download_sizes;

		my $max_simultaneous_downloads_allowed = $self->{_config}->var('cupt::downloader::max-simultaneous-downloads');
		pipe(SELF_READ, SELF_WRITE) or
				mydie("cannot create worker's own pipe");
		autoflush SELF_WRITE;

		my $exit_flag = 0;
		while (!$exit_flag) {
			my @ready = IO::Select->new(\*SELF_READ, \*STDIN, map { $_->{input_fh} } values %active_downloads)->can_read();
			foreach my $fh (@ready) {
				my @params = __my_read_pipe($fh);
				my $command = shift @params;
				my $uri;
				my $filename;
				my $waiter_fh;

				my $proceed_next_download = 0;
				given ($command) {
					when ('exit') { $exit_flag = 1; }
					when ('download') {
						# new query appeared
						($uri, $filename, my $waiter_fifo) = @params;
						open($waiter_fh, ">", $waiter_fifo) or
								mydie("unable to connect to download fifo for '$uri' -> '$filename': $!");
						autoflush $waiter_fh;
						# check if this download was already done
						if (exists $done_downloads{$uri,$filename}) {
							# just end it
							__my_write_pipe(\*SELF_WRITE, 'done', $uri, $filename, 1, '');
						} elsif (scalar keys %active_downloads >= $max_simultaneous_downloads_allowed) {
							# put the query on hold
							push @waiting_downloads, [ $uri, $filename, $waiter_fh ];
						} else {
							$proceed_next_download = 1;
						}
					}
					when ('set-download-size') {
						($uri, my $size) = @params;
						$download_sizes{$uri} = $size;
					}
					when ('done') {
						# some query ended
						($uri, $filename, my $result) = @params;
						# send an answer for a download
						__my_write_pipe($active_downloads{$uri,$filename}->{waiter_fh}, $result);

						# update progress
						$self->{_progress}->progress($uri, 'done');

						# clean after child
						close($active_downloads{$uri,$filename}->{input_fh});
						close($active_downloads{$uri,$filename}->{waiter_fh});
						waitpid($active_downloads{$uri,$filename}->{pid}, 0);
						# removing the query from active download list and put it to
						# the list of ended ones
						delete $active_downloads{$uri,$filename};
						$done_downloads{$uri,$filename} = 1;

						if (scalar @waiting_downloads && (scalar keys %active_downloads < $max_simultaneous_downloads_allowed)) {
							# put next of waiting queries
							($uri, $filename, $waiter_fh) = @{shift @waiting_downloads};
							$proceed_next_download = 1;
						}

					}
					when ('progress') {
						# update progress
						$self->{_progress}->progress(@params);
					}
					default { myinternaldie("download manager: invalid worker command"); }
				}
				$proceed_next_download or next;
				# filling the active downloads hash
				$active_downloads{$uri,$filename}->{waiter_fh} = $waiter_fh;
				# there is a space for new download, start it

				# start progress
				my $size = $download_sizes{$uri};
				__my_write_pipe(\*SELF_WRITE, 'progress', $uri, 'start', $size);

				my $download_pid = open(my $download_fh, "-|");
				$download_pid // myinternaldie("unable to fork: $!");

				$active_downloads{$uri,$filename}->{pid} = $download_pid;
				$active_downloads{$uri,$filename}->{input_fh} = $download_fh;

				if ($download_pid) {
					# worker process, nothing to do, go ahead
				} else {
					# background downloader process
					autoflush STDOUT;

					select STDERR;
					my $result = $self->_download($uri, $filename);
					__my_write_pipe(\*STDOUT, 'done', $uri, $filename, $result);
					POSIX::_exit(0);
				}
			}
		}
		close STDIN or mydie("unable to close STDIN for worker: $!");
		close SELF_WRITE or mydie("unable to close writing side of worker's own pipe: $!");
		close SELF_READ or mydie("unable to close reading side of worker's own pipe: $!");
		POSIX::_exit(0);
	}
}

sub DESTROY {
	my ($self) = @_;
	# shutdowning worker thread
	__my_write_pipe($self->{_worker_fh}, 'exit');
	waitpid($self->{_worker_pid}, 0);
}

=head2 set_size_for_uri

method, set fixed download size for uri

Parameters:

I<uri> - URI

I<size> - fixed download size in bytes

=cut

sub set_size_for_uri {
	my ($self, $uri, $size) = @_;
	__my_write_pipe($self->{_worker_fh}, 'set-download-size', $uri, $size);
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
otherwise empty

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

	my @waiters;
	# schedule download of each uri at its own thread
	while (scalar @_) {
		# extract next pair
		my $uri = shift;
		my $filename = shift;

		# schedule new download

		my $waiter_fifo = File::Temp::tempnam($self->{_fifo_dir}, "download-") or
				mydie("unable to choose name for download fifo for '$uri' -> '$filename': $!");
		system('mkfifo', '-m', '600', $waiter_fifo) == 0 or
				mydie("unable to create download fifo for '$uri' -> '$filename': $?");

		flock($self->{_worker_fh}, LOCK_EX);
		__my_write_pipe($self->{_worker_fh}, 'download', $uri, $filename, $waiter_fifo);
		flock($self->{_worker_fh}, LOCK_UN);

		open(my $waiter_fh, "<", $waiter_fifo) or
				mydie("unable to listen to download fifo: $!");

		push @waiters, { 'fifo' => $waiter_fifo, 'fh' => $waiter_fh };
	}

	# all are scheduled successfully, wait for them
	my $result = 0;
	while (scalar @waiters) {
		my @ready = IO::Select->new(map { $_->{fh} } @waiters)->can_read();
		foreach my $waiter_fh (@ready) {
			# find appropriate fifo file string for file handle
			my $waiter_idx;
			foreach my $idx (0..$#waiters) {
				if (fileno($waiters[$idx]->{fh}) == fileno($waiter_fh)) {
					$waiter_idx = $idx;
					last;
				}
			}
			my $waiter_fifo = $waiters[$waiter_idx]->{fifo};

			my ($current_result) = __my_read_pipe($waiter_fh);
			close($waiter_fh) or
					mydie("unable to close download fifo: $!");

			# remove fifo from system
			unlink $waiter_fifo;

			# delete from entry from list
			splice @waiters, $waiter_idx, 1;

			if ($current_result ne '0') {
				# this download hasn't been processed smoothly
				$result = $current_result;
			}
		}
	}

	# finish
	return $result;
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
		__my_write_pipe(\*STDOUT, 'progress', $uri, @_);
	};
	return $handler->perform($self->{_config}, $uri, $filename, $sub_callback);
}

1;

