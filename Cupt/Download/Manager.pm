#***************************************************************************
#*   Copyright (C) 2008-2009 by Eugene V. Lyubimkin                        *
#*                                                                         *
#*   This program is free software; you can redistribute it and/or modify  *
#*   it under the terms of the GNU General Public License                  *
#*   (version 3 or above) as published by the Free Software Foundation.    *
#*                                                                         *
#*   This program is distributed in the hope that it will be useful,       *
#*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
#*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
#*   GNU General Public License for more details.                          *
#*                                                                         *
#*   You should have received a copy of the GNU GPL                        *
#*   along with this program; if not, write to the                         *
#*   Free Software Foundation, Inc.,                                       *
#*   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA               *
#*                                                                         *
#*   This program is free software; you can redistribute it and/or modify  *
#*   it under the terms of the Artistic License, which comes with Perl     *
#***************************************************************************
package Cupt::Download::Manager;

=head1 NAME

Cupt::Download::Manager - file download manager for Cupt

=cut

use 5.10.0;
use strict;
use warnings;

use URI;
use IO::Handle;
use IO::Select;
use Fcntl qw(:flock);
use File::Temp;
use POSIX;
use Time::HiRes qw(setitimer ITIMER_REAL);

use fields qw(_config _progress _downloads_done _worker_fh _worker_pid _fifo_dir);

use Cupt::Core;
use Cupt::Download::Methods::Curl;
use Cupt::Download::Methods::File;

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
	my $read_result = sysread $fh, $packed_len, 2;
	$read_result or mydie("attempt to read from closed pipe");
	my ($len) = unpack("S", $packed_len);
	my $string;
	sysread $fh, $string, $len;
	return split(chr(0), $string);
}

=head1 METHODS

=head2 new

creates new Cupt::Download::Manager and returns reference to it

Parameters:

I<config> - reference to L<Cupt::Config|Cupt::Config>

I<progress> - reference to object of subclass of L<Cupt::Download::Progress|Cupt::Download::Progress>

=cut

sub new ($$$) {
	my $class = shift;
	my $self;
	$self = fields::new($class);
	$self->{_config} = shift;
	$self->{_progress} = shift;

	# making fifo storage dir if it's absend
	$self->{_fifo_dir} = File::Temp::tempdir('cupt-XXXXXX', CLEANUP => 1, TMPDIR => 1) or
			mydie("unable to create temporary directory for fifo storage: %s", $!);

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

		# { $uri => 1 }
		my %done_downloads;
		# { $uri => $waiter_fh, $pid, $input_fh }
		my %active_downloads;
		# [ $uri, $filename, $filehandle ]
		my @waiting_downloads;
		# { $uri => $size }
		my %download_sizes;
		# { $uri => $filename }
		my %target_filenames;

		my $max_simultaneous_downloads_allowed = $self->{_config}->var('cupt::downloader::max-simultaneous-downloads');
		pipe(SELF_READ, SELF_WRITE) or
				mydie("cannot create worker's own pipe");
		autoflush SELF_WRITE;

		my $exit_flag = 0;

		# setting progress ping timer
		$SIG{ALRM} = sub { __my_write_pipe(\*SELF_WRITE, 'progress', '', 'ping') };
		setitimer(ITIMER_REAL, 0, 0.25);

		while (!$exit_flag) {
			my @ready = IO::Select->new(\*SELF_READ, \*STDIN, map { $_->{input_fh} } values %active_downloads)->can_read();
			foreach my $fh (@ready) {
				next unless $fh->opened;
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
								mydie("unable to connect to download fifo for '%s' -> '%s': %s", $uri, $filename, $!);
						autoflush $waiter_fh;
						# check if this download was already done
						if (exists $done_downloads{$uri}) {
							# just end it
							__my_write_pipe(\*SELF_WRITE, 'done', $uri, 1, '');
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
						($uri, my $result) = @params;
						# send an answer for a download
						__my_write_pipe($active_downloads{$uri}->{waiter_fh}, $result);

						# update progress
						__my_write_pipe(\*SELF_WRITE, 'progress', $uri, 'done', $result);

						# clean after child
						close($active_downloads{$uri}->{input_fh});
						close($active_downloads{$uri}->{waiter_fh});
						waitpid($active_downloads{$uri}->{pid}, 0);
						# removing the query from active download list and put it to
						# the list of ended ones
						delete $active_downloads{$uri};
						$done_downloads{$uri} = 1;

						if (scalar @waiting_downloads && (scalar keys %active_downloads < $max_simultaneous_downloads_allowed)) {
							# put next of waiting queries
							($uri, $filename, $waiter_fh) = @{shift @waiting_downloads};
							$proceed_next_download = 1;
						}
					}
					when ('progress') {
						$uri = shift @params;
						my $action = shift @params;
						if ($action eq 'expected-size' && exists $download_sizes{$uri}) {
							# ok, we knew what size we should get, and the method has reported his variant
							# now compare them strictly
							my $expected_size = shift @params;
							if ($expected_size != $download_sizes{$uri}) {
								# so, this download don't make sense
								$filename = $target_filenames{$uri};
								# rest in peace, young process
								kill SIGTERM, $active_downloads{$uri}->{pid};
								# process it as failed
								__my_write_pipe(\*SELF_WRITE, 'done', $uri, 'invalid size');
								unlink $filename;
							}
						} else {
							# update progress
							$self->{_progress}->progress($uri, $action, @params);
						}
					}
					default { myinternaldie("download manager: invalid worker command"); }
				}
				$proceed_next_download or next;
				# there is a space for new download, start it

				$target_filenames{$uri} = $filename;
				# filling the active downloads hash
				$active_downloads{$uri}->{waiter_fh} = $waiter_fh;

				my $download_pid = open(my $download_fh, "-|");
				$download_pid // myinternaldie("unable to fork: $!");

				$active_downloads{$uri}->{pid} = $download_pid;
				$active_downloads{$uri}->{input_fh} = $download_fh;

				if ($download_pid) {
					# worker process, nothing to do, go ahead
				} else {
					# background downloader process
					$SIG{TERM} = sub { POSIX::_exit(0) };

					autoflush STDOUT;

					# start progress
					my @progress_message = ('progress', $uri, 'start');
					my $size = $download_sizes{$uri};
					push @progress_message, $size if defined $size;
					__my_write_pipe(\*STDOUT, @progress_message);

					my $result = $self->_download($uri, $filename);
					__my_write_pipe(\*STDOUT, 'done', $uri, $result);
					close(STDOUT) or
							mydie("unable to close STDOUT");
					POSIX::_exit(0);
				}
			}
		}
		# disabling timer
		$SIG{ALRM} = sub {};
		setitimer(ITIMER_REAL, 0, 0);
		# finishing progress
		$self->{_progress}->finish();

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

=head2 download

method, adds group of download queries to queue. Blocks execution of program until
all downloads are done.

Parameters:

Sequence of hash entries with the following fields:

I<uri> - URI to download, mandatory

I<filename> - target filename, mandatory

I<post-action> - reference to subroutine that will be called in case of
successful download, optional

I<size> - fixed size for target, will be used in sanity checks, optional

Returns:

I<result> - '0' on success, otherwise the string that contains the fail reason,

Example:

  my $download_manager = new Cupt::Download::Manager;
  $download_manager->download(
	  { 'uri' => 'http://www.en.debian.org', 'filename' => '/tmp/en.html' },
	  { 'uri' => 'http://www.ru.debian.org', 'filename' => '/tmp/ru.html', 'post-action' => \&checker },
	  { 'uri' => 'http://www.ua.debian.org', 'filename' => '/tmp/ua.html', 'size' => 10254 }
  );

=cut

sub download ($@) {
	my $self = shift;

	my @waiters;
	# schedule download of each uri at its own thread
	while (scalar @_) {
		# extract next entry
		my $ref_entry = shift;
		my $uri = $ref_entry->{'uri'};
		my $filename = $ref_entry->{'filename'};

		# schedule new download

		my $waiter_fifo = File::Temp::tempnam($self->{_fifo_dir}, "download-") or
				mydie("unable to choose name for download fifo for '$uri' -> '$filename': $!");
		system('mkfifo', '-m', '600', $waiter_fifo) == 0 or
				mydie("unable to create download fifo for '$uri' -> '$filename': $?");

		flock($self->{_worker_fh}, LOCK_EX);
		if (exists $ref_entry->{'size'}) {
			__my_write_pipe($self->{_worker_fh}, 'set-download-size', $uri, $ref_entry->{'size'});
		}
		__my_write_pipe($self->{_worker_fh}, 'download', $uri, $filename, $waiter_fifo);
		flock($self->{_worker_fh}, LOCK_UN);

		open(my $waiter_fh, "<", $waiter_fifo) or
				mydie("unable to listen to download fifo: $!");

		push @waiters, { 'fifo' => $waiter_fifo, 'fh' => $waiter_fh, 'checker' => $ref_entry->{'post-action'} };
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
			my $waiter_fifo = $waiters[$waiter_idx]->{'fifo'};
			my $sub_post_action = $waiters[$waiter_idx]->{'checker'};

			my ($current_result) = __my_read_pipe($waiter_fh);
			close($waiter_fh) or
					mydie("unable to close download fifo: $!");

			# remove fifo from system
			unlink $waiter_fifo;

			# delete from entry from list
			splice @waiters, $waiter_idx, 1;

			if ($current_result eq '0' && defined $sub_post_action) {
				# download seems to be done well, but we also have external checker specified
				$current_result = $sub_post_action->();
			}

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
		'file' => 'File',
	);
	my $protocol = URI->new($uri)->scheme();
	my $handler_name = $protocol_handlers{$protocol} // 
			return sprintf __("no protocol download handler defined for %s"), $protocol;

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

