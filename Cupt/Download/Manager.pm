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

use IO::Select;
use IO::Pipe;
use IO::Socket::UNIX;
use POSIX;
use Time::HiRes qw(setitimer ITIMER_REAL);

use fields qw(_config _progress _worker_pid _server_socket _parent_pipe _server_socket_path);

use Cupt::Core;
use Cupt::Download::Method;

sub __my_write_socket ($@) {
	my $socket = shift;
	defined $socket or myinternaldie('bad socket parameter');

	my $string = join(chr(1), @_);
	my $len = length($string);
	my $packed_len = pack('S', $len);

	syswrite($socket, ($packed_len . $string)) or
			myinternaldie("write to socket failed: $!");
	return;
}

sub __my_read_socket ($) {
	my $socket = shift;
	defined $socket or myinternaldie('bad socket parameter');

	my $string;

	my $read_result = sysread($socket, my $packed_len, 2);
	defined $read_result or myinternaldie("read from socket failed: $!");

	if ($read_result == 0) {
		$string = 'eof';
	} else {
		my ($len) = unpack('S', $packed_len);
		# don't use anything but sysread here, as we use select() on sockets
		sysread $socket, $string, $len;
	}

	return split(chr(1), $string, -1);
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

	# main socket
	$self->{_server_socket_path} = "/tmp/cupt-downloader-$$";
	unlink($self->{_server_socket_path}); # intentionally ignore errors
	$self->{_server_socket} = IO::Socket::UNIX->new(
			Local => $self->{_server_socket_path}, Listen => SOMAXCONN, Type => SOCK_STREAM);
	defined $self->{_server_socket} or
			mydie("unable to open server socket on file '%s': %s", $self->{_server_socket_path}, $!);

	$self->{_parent_pipe} = IO::Pipe->new() //
			mydie('unable to open parent pipe: %s', $!);

	my $pid = fork();
	defined $pid or
			mydie('unable to create download worker process: %s', $!);

	if ($pid) {
		# this is a main process
		$self->{_worker_pid} = $pid;
		$self->{_parent_pipe}->writer();
		$self->{_parent_pipe}->autoflush(1);
		return $self;
	} else {
		# this is background worker process
		$self->{_parent_pipe}->reader();
		$self->_worker();
	}
	return;
}

sub _worker ($) {
	my ($self) = @_;

	my $debug = $self->{_config}->var('debug::downloader');

	mydebug('download worker process started') if $debug;

	# { $uri => $result }
	my %done_downloads;
	# { $uri => $waiter_socket, $pid, $performer_reader }
	my %active_downloads;
	# [ $uri, $filename, $filehandle ]
	my @download_queue;
	# the downloads that are already scheduled but not completed, and another waiter fifo appeared
	# [ $uri, $filehandle ]
	my @pending_downloads;
	# { $uri => $size }
	my %download_sizes;
	# { $uri => $filename }
	my %target_filenames;

	my $max_simultaneous_downloads_allowed = $self->{_config}->var('cupt::downloader::max-simultaneous-downloads');
	pipe(my $worker_reader, my $worker_writer) or
			mydie("unable to open worker's own pair of sockets: %s", $!);
	$worker_writer->autoflush(1);

	my $exit_flag = 0;

	# setting progress ping timer
	local $SIG{ALRM} = sub { __my_write_socket($worker_writer, 'progress', '', 'ping') };
	setitimer(ITIMER_REAL, 0.25, 0.25);

	my @persistent_sockets = ($worker_reader, $self->{_parent_pipe}, $self->{_server_socket});
	my @runtime_sockets;

	# while caller may set exit flag, we should continue processing as long as
	# something is pending in internal queue
	while (!$exit_flag || IO::Select->new($worker_reader)->can_read(0)) {
		# periodic cleaning of sockets which were accept()'ed
		@runtime_sockets = grep { $_->opened } @runtime_sockets;

		my @ready = IO::Select->new(@persistent_sockets, @runtime_sockets,
				map { $_->{performer_reader} } values %active_downloads)->can_read();

		foreach my $socket (@ready) {
			next if not $socket->opened;
			if ($socket eq $self->{_server_socket}) {
				# a new connection appeared
				$socket = $socket->accept();
				defined $socket or
						mydie('unable to accept new socket connection: %s', $!);
				mydebug('accepted new connection') if $debug;
				push @runtime_sockets, $socket;
			}
			my @params = __my_read_socket($socket);
			my $command = shift @params;
			my $uri;
			my $filename;
			my $waiter_socket;

			my $proceed_next_download = 0;
			given ($command) {
				when ('exit') {
					mydebug('exit scheduled') if $debug;
					$exit_flag = 1;
				}
				when ('eof') {
					# the current socket reported EOF
					mydebug('eof has been reported') if $debug;
					close($socket) or mydie('unable to close socket: %s', $!);
				}
				when ('download') {
					# new query appeared
					($uri, $filename) = @params;
					$waiter_socket = $socket;
					mydebug("download request: '$uri'") if $debug;
					$proceed_next_download = 1;
				}
				when ('set-download-size') {
					($uri, my $size) = @params;
					$download_sizes{$uri} = $size;
				}
				when ('done') {
					# some query ended, we have preliminary result for it
					scalar @params == 2 or
							myinternaldie("bad argument count for 'done' message");

					($uri, my $result) = @params;
					mydebug("preliminary download result: '$uri': $result") if $debug;
					my $is_duplicated_download = 0;
					__my_write_socket($active_downloads{$uri}->{waiter_socket}, $uri, $result, $is_duplicated_download);

					# clean after child
					close($active_downloads{$uri}->{performer_reader}) or
							mydie('unable to close performer socket: %s', $!);

					waitpid($active_downloads{$uri}->{pid}, 0);
				}
				when ('done-ack') {
					# this is final ACK from download with final result
					scalar @params == 2 or
							myinternaldie("bad argument count for 'done-ack' message");

					($uri, my $result) = @params;
					mydebug("final download result: '$uri': $result") if $debug;

					# removing the query from active download list and put it to
					# the list of ended ones
					delete $active_downloads{$uri};
					$done_downloads{$uri} = $result;

					mydebug('started checking pending queue') if $debug;
					do { # answering on duplicated requests if any
						my $is_duplicated_download = 1;
						my @new_pending_downloads;

						foreach my $ref_pending_download (@pending_downloads) {
							(my $pending_uri, $waiter_socket) = @$ref_pending_download;
							if (exists $done_downloads{$pending_uri}) {
								mydebug("final download result for duplicated request: '$pending_uri': $result") if $debug;
								__my_write_socket($waiter_socket, $pending_uri, $result, $is_duplicated_download);
								close($waiter_socket) or
										mydie("unable to close waiter socket for uri '%s'", $pending_uri);
							} else {
								push @new_pending_downloads, $ref_pending_download;
							}
						}
						@pending_downloads = @new_pending_downloads;
					};
					mydebug('finished checking pending queue') if $debug;

					# update progress
					__my_write_socket($worker_writer, 'progress', $uri, 'done', $result);

					# schedule next download
					__my_write_socket($worker_writer, 'pop-download');
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
							my $error_string = sprintf __("invalid size: expected '%u', got '%u'"),
									$download_sizes{$uri}, $expected_size;
							__my_write_socket($worker_writer, 'done', $uri, $error_string);
							unlink $filename;
						}
					} else {
						# update progress
						$self->{_progress}->progress($uri, $action, @params);
					}
				}
				when ('pop-download') {
					if (scalar @download_queue) {
						# put next of waiting queries
						($uri, $filename, $waiter_socket) = @{shift @download_queue};
						mydebug("enqueue '$uri' from hold") if $debug;
						$proceed_next_download = 1;
					}
				}
				when ('set-long-alias') {
					$self->{_progress}->set_long_alias_for_uri(@params);
				}
				when ('set-short-alias') {
					$self->{_progress}->set_short_alias_for_uri(@params);
				}
				default { myinternaldie('download manager: invalid worker command'); }
			}

			$proceed_next_download or next;
			mydebug("processing download '$uri'") if $debug;

			# check if this download was already done
			if (exists $done_downloads{$uri}) {
				my $result = $done_downloads{$uri};
				mydebug("final result for duplicated download '$uri': $result") if $debug;
				# just immediately end it
				my $is_duplicated_download = 1;
				__my_write_socket($waiter_socket, $uri, $result, $is_duplicated_download);

				# schedule next download
				__my_write_socket($worker_writer, 'pop-download');
				next;
			} elsif (exists $active_downloads{$uri}) {
				mydebug("pushed '$uri' to pending queue") if $debug;
				push @pending_downloads, [ $uri, $waiter_socket ];
				next;
			} elsif (scalar keys %active_downloads >= $max_simultaneous_downloads_allowed) {
				# put the query on hold
				mydebug("put '$uri' on hold") if $debug;
				push @download_queue, [ $uri, $filename, $waiter_socket ];
				next;
			}

			# there is a space for new download, start it

			mydebug("starting download '$uri'") if $debug;
			$target_filenames{$uri} = $filename;
			# filling the active downloads hash
			$active_downloads{$uri}->{waiter_socket} = $waiter_socket;

			my $performer_pipe = IO::Pipe->new() //
					mydie("unable to open performer's pair of sockets: %s", $!);

			my $download_pid = fork();
			$download_pid // mydie('unable to fork: %s', $!);
			$active_downloads{$uri}->{pid} = $download_pid;

			if ($download_pid) {
				# worker process, nothing to do, go ahead
				$performer_pipe->reader();
				$active_downloads{$uri}->{performer_reader} = $performer_pipe;
			} else {
				# background downloader process
				local $SIG{TERM} = sub { POSIX::_exit(0) };

				$performer_pipe->writer();
				$performer_pipe->autoflush(1);

				# start progress
				my @progress_message = ('progress', $uri, 'start');
				my $size = $download_sizes{$uri};
				push @progress_message, $size if defined $size;

				__my_write_socket($performer_pipe, @progress_message);

				my $result = $self->_download($uri, $filename, $performer_pipe);
				myinternaldie('a download method returned undefined result') if not defined $result;

				__my_write_socket($performer_pipe, 'done', $uri, $result);

				POSIX::_exit(0);
			}
		}
	}
	# disabling timer
	local $SIG{ALRM} = sub {};
	setitimer(ITIMER_REAL, 0, 0);
	# finishing progress
	$self->{_progress}->finish();

	close($worker_reader) or mydie("unable to close worker's own reader socket: %s", $!);
	close($worker_writer) or mydie("unable to close worker's own writer socket: %s", $!);
	mydebug('download worker process finished') if $debug;
	POSIX::_exit(0);
	return;
}

sub DESTROY {
	my ($self) = @_;
	# shutdowning worker thread
	__my_write_socket($self->{_parent_pipe}, 'exit');
	waitpid($self->{_worker_pid}, 0);

	# cleaning parent sockets
	close($self->{_parent_pipe}) or mydie('unable to close parent writer socket: %s', $!);

	# cleaning server socket
	close($self->{_server_socket}) or
			mydie("unable to close server socket on file '%s': %s", $self->{_server_socket_path}, $!);
	unlink($self->{_server_socket_path}) or
			mydie("unable to delete server socket file '%s': %s", $self->{_server_socket_path}, $!);
	return;
}

=head2 download

method, adds group of download queries to queue. Blocks execution of program until
all downloads are done.

This method is re-entrant.

Parameters:

Sequence of hash entries with the following fields:

I<uris> - array of mirror URIs to download, mandatory

I<filename> - target filename, mandatory

I<post-action> - reference to subroutine that will be called in case of
successful download, optional

I<size> - fixed size for target, will be used in sanity checks, optional

Returns:

I<result> - '0' on success, otherwise the string that contains the fail reason,

Example:

  my $download_manager = new Cupt::Download::Manager;
  $download_manager->download(
    { 'uris' => [ 'http://www.en.debian.org' ], 'filename' => '/tmp/en.html' },
    { 'uris' => [ 'http://www.ru.debian.org' ], 'filename' => '/tmp/ru.html', 'post-action' => \&checker },
    { 'uris' => [ 'http://www.ua.debian.org' ], 'filename' => '/tmp/ua.html', 'size' => 10254 }
    { 'uris' => [
        'http://ftp.de.debian.org/debian/pool/main/n/nlkt/nlkt_0.3.2.1-2_amd64.deb',
        'http://ftp.es.debian.org/debian/pool/main/n/nlkt/nlkt_0.3.2.1-2_amd64.deb'
      ], 'filename' => '/var/cache/apt/archives/nlkt_0.3.2.1-2_amd64.deb' }
  );

=cut

sub download ($@) {
	my $self = shift;

	# { $filename => { 'uris' => [ $uri... ], 'size' => $size, 'checker' => $checker }... }
	my %download_entries;

	# { $uri => $filename }
	my %waiters;

	my $socket = IO::Socket::UNIX->new($self->{_server_socket_path});
	defined $socket or mydie("unable to open download socket: %s", $!);

	my $sub_schedule_download = sub {
		my ($filename) = @_;

		$download_entries{$filename}->{'current_uri'} = shift @{$download_entries{$filename}->{'uris'}};
		my $uri = $download_entries{$filename}->{'current_uri'};
		my $size = $download_entries{$filename}->{'size'};
		my $checker = $download_entries{$filename}->{'checker'};

		if (defined $size) {
			__my_write_socket($socket, 'set-download-size', $uri, $size);
		}
		__my_write_socket($socket, 'download', $uri, $filename);

		$waiters{$uri} = $filename;
	};

	# schedule download of each uri at its own thread
	while (scalar @_) {
		# extract next entry
		my $ref_entry = shift;
		my $ref_uris= $ref_entry->{'uris'};
		my $filename = $ref_entry->{'filename'};

		$download_entries{$filename}->{'uris'} = $ref_uris;
		# may be undef
		$download_entries{$filename}->{'size'} = $ref_entry->{'size'};
		# may be undef
		$download_entries{$filename}->{'checker'} = $ref_entry->{'post-action'};

		$sub_schedule_download->($filename);
	}

	# all are scheduled successfully, wait for them
	my $result = 0;
	while (scalar keys %waiters) {
		my ($uri, $error_string, $is_duplicated_download) = __my_read_socket($socket);
		my $filename = $waiters{$uri};
		my $sub_post_action = $download_entries{$filename}->{'checker'};

		# delete entry from list
		delete $waiters{$uri};

		if (!$error_string && defined $sub_post_action && not $is_duplicated_download) {
			# download seems to be done well, but we also have external checker specified
			# but do this only if this file wasn't post-processed before
			$error_string = $sub_post_action->();
		}

		if (not $is_duplicated_download) {
			# now we know final result, send it back (for progress indicator)
			__my_write_socket($socket, 'done-ack', $uri, $error_string);
		}

		if ($error_string) {
			# this download hasn't been processed smoothly
			# check - maybe we have another URIs for this file
			if (scalar @{$download_entries{$filename}->{'uris'}}) {
				# yes, so reschedule a download with another URI
				$sub_schedule_download->($filename);
			} else {
				# no, this URI was last
				$result = $error_string;
			}
		}
	}

	close($socket) or mydie("unable to close download socket: %s", $!);

	# finish
	return $result;
}

=head2 set_short_alias_for_uri

method, forwards params to underlying download progress

=cut

sub set_short_alias_for_uri {
	my ($self, @params) = @_;
	__my_write_socket($self->{_parent_pipe}, 'set-short-alias', @params);
}

=head2 set_long_alias_for_uri

method, forwards params to underlying download progress

=cut

sub set_long_alias_for_uri {
	my ($self, @params) = @_;
	__my_write_socket($self->{_parent_pipe}, 'set-long-alias', @params);
}

sub _download ($$$) {
	my ($self, $uri, $filename, $socket) = @_;

	my $sub_callback = sub {
		__my_write_socket($socket, 'progress', $uri, @_);
	};
	my $download_method = new Cupt::Download::Method;
	my $result = $download_method->perform($self->{_config}, $uri, $filename, $sub_callback);
	return $result;
}

1;

