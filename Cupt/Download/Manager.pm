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
use IO::Select;
use IO::Socket::UNIX;
use File::Temp qw(tempfile);
use POSIX;
use Time::HiRes qw(setitimer ITIMER_REAL);

use fields qw(_config _progress _worker_pid _server_socket _socket _server_socket_path);

use Cupt::Core;
use Cupt::Download::Methods::Curl;
use Cupt::Download::Methods::File;

sub __my_write_socket ($@) {
	my $socket = shift;
	my $string = join(chr(0), @_);
	my $len = length($string);
	my $packed_len = pack("S", $len);
	$socket->send($packed_len . $string, 0);
}

sub __my_read_socket ($) {
	my $socket = shift;
	my $packed_len;
	my $read_result = $socket->recv($packed_len, 2, 0);
	$read_result or mydie("attempt to read from closed socket");
	my ($len) = unpack("S", $packed_len);
	my $string;
	$socket->recv($string, $len, 0);
	return split(chr(0), $string, -1);
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
	unlink($self->{_socket_path}); # intentionally ignore errors
	$self->{_server_socket} = IO::Socket::UNIX(
			Local => $self->{_server_socket_path}, Listen => 1, Type => SOCK_STREAM);
	defined $self->{_server_socket} or
			mydie("unable to create server socket on file '%s'", $self->{_server_socket_path});
	
	$self->{_socket} = new IO::Socket::UNIX($self->{_socket_path});
	defined $self->{_socket} or
			mydie("unable to create client socket");

	my $pid = fork();
	defined $pid or
			mydie("unable to create download worker process: %s", $!);

	if ($pid) {
		# this is a main process
		$self->{_worker_pid} = $pid;
		return $self;
	} else {
		# this is background worker process
		$self->_worker();
	}
}

sub _worker ($) {
	my ($self) = @_;

	my $debug = $self->{_config}->var('debug::downloader');

	mydebug("download worker process started") if $debug;

	# { $uri => $result }
	my %done_downloads;
	# { $uri => $waiter_socket, $pid, $input_fh }
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
	my $worker_socket = new IO::Socket::UNIX($self->{_server_socket_path});
	defined $self_write_socket or
			mydie("unable to create worker's own socket connection");

	my $exit_flag = 0;

	# setting progress ping timer
	$SIG{ALRM} = sub { __my_write_socket(\*SELF_WRITE, 'progress', '', 'ping') };
	setitimer(ITIMER_REAL, 0.25, 0.25);

	my @persistent_sockets = ($worker_socket, $self->{_socket}, $self->{_server_socket});
	while (!$exit_flag) {
		my @ready = IO::Select->new(@persistent_sockets, map { $_->{input_socket} } values %active_downloads)->can_read();
		foreach my $socket (@ready) {
			next unless $socket->opened;
			if ($socket eq $self->{_server_socket}) {
				# a new connection appeared
				$socket = $socket->accept();
				defined $socket or
						mydie("unable to accept new socket connection");
			}
			my @params = __my_read_socket($socket);
			my $command = shift @params;
			my $uri;
			my $filename;
			my $waiter_socket;

			my $proceed_next_download = 0;
			given ($command) {
				when ('exit') { $exit_flag = 1; }
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
					close($active_downloads{$uri}->{input_socket});
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

					mydebug("started checking pending queue") if $debug;
					do { # answering on duplicated requests if any
						my $is_duplicated_download = 1;
						my @new_pending_downloads;

						foreach my $ref_pending_download (@pending_downloads) {
							(my $uri, $waiter_socket) = @$ref_pending_download;
							if (exists $done_downloads{$uri}) {
								mydebug("final download result for duplicated request: '$uri': $result") if $debug;
								__my_write_socket($waiter_socket, $uri, $result, $is_duplicated_download);
								close($waiter_socket);
							} else {
								push @new_pending_downloads, $ref_pending_download;
							}
						}
						@pending_downloads = @new_pending_downloads;
					};
					mydebug("finished checking pending queue") if $debug;

					# update progress
					__my_write_socket($worker_socket, 'progress', $uri, 'done', $result);

					# schedule next download
					__my_write_socket($worker_socket, 'pop-download');
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
							__my_write_socket($worker_socket, 'done', $uri, $error_string);
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
						($uri, $filename, $waiter_fh) = @{shift @download_queue};
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
				default { myinternaldie("download manager: invalid worker command"); }
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
				__my_write_socket(\*SELF_WRITE, 'pop-download');
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

			my $download_pid = open(my $download_fh, "-|");
			$download_pid // mydie("unable to fork: %s", $!);

			$active_downloads{$uri}->{pid} = $download_pid;
			$active_downloads{$uri}->{input_fh} = $download_fh;

			if ($download_pid) {
				# worker process, nothing to do, go ahead
			} else {
				# background downloader process
				$SIG{TERM} = sub { POSIX::_exit(0) };

				# start progress
				my @progress_message = ('progress', $uri, 'start');
				my $size = $download_sizes{$uri};
				push @progress_message, $size if defined $size;
				__my_write_socket(\*STDOUT, @progress_message);

				my $result = $self->_download($uri, $filename);
				myinternaldie("a download method returned undefined result") if not defined $result;
				__my_write_socket(\*STDOUT, 'done', $uri, $result);
				close(STDOUT) or
						mydie("unable to close standard output");
				POSIX::_exit(0);
			}
		}
	}
	# disabling timer
	$SIG{ALRM} = sub {};
	setitimer(ITIMER_REAL, 0, 0);
	# finishing progress
	$self->{_progress}->finish();

	close($worker_socket) or mydie("unable create worker's own socket connection");
	close($self->{_socket}) or mydie("unable to close parent socket connection", $!);
	close($self->{_server_socket}) or mydie("unable to close server socket", $!);
	mydebug("download worker process finished") if $debug;
	POSIX::_exit(0);
}

sub DESTROY {
	my ($self) = @_;
	# shutdowning worker thread
	__my_write_socket($self->{_socket}, 'exit');
	waitpid($self->{_worker_pid}, 0);

	# cleaning server socket
	close($self->{_server_socket}) or
			mydie("unable to close server socket on file '%s'", $self->{_server_socket_path});
	unlink($self->{_server_socket_path) or
			mydie("unable to delete server socket file '%s'", $self->{_server_socket_path});
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

	my $socket = new IO::Socket::UNIX($self->{_server_socket_path});
	defined $socket or mydie("unable to close download socket: %s", $!);

	my $sub_schedule_download = sub {
		my ($filename) = @_;

		$download_entries{$filename}->{'current_uri'} = shift @{$download_entries{$filename}->{'uris'}};
		my $uri = $download_entries{$filename}->{'current_uri'};
		my $size = $download_entries{$filename}->{'size'};
		my $checker = $download_entries{$filename}->{'checker'};

		if (defined $size) {
			__my_write_socket($socket, 'set-download-size', $uri, $size);
		}
		__my_write_socket($socket, 'download', $uri, $filename, $waiter_fifo);

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
	__my_write_socket($self->{_socket}, 'set-short-alias', @params);
}

=head2 set_long_alias_for_uri

method, forwards params to underlying download progress

=cut

sub set_long_alias_for_uri {
	my ($self, @params) = @_;
	__my_write_socket($self->{_worker_fh}, 'set-long-alias', @params);
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

	my $socket = new IO::Socket::UNIX($self->{_server_socket_path});
	defined $socket or mydie("unable to close performer socket: %s", $!);

	# download the file
	my $sub_callback = sub {
		__my_write_socket(\*STDOUT, 'progress', $uri, @_);
	};
	my $result = $handler->perform($self->{_config}, $uri, $filename, $sub_callback);
	close($socket) or mydie("unable to close performer socket: %s", $!);
	return $result;
}

1;

