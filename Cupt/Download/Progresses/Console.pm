package Cupt::Download::Progresses::Console;

=head1 NAME

Cupt::Download::Progresses::Console - console download progress for Cupt

=cut

use 5.10.0;
use warnings;
use strict;

use base qw(Cupt::Download::Progress);

use List::Util qw(sum);
use Term::Size;

use Cupt::Core;

sub new {
	my $class = shift;
	my $self = $class->SUPER::new();
	$self->{_now_downloading} = {};
	$self->{_next_download_number} = 1;
	$self->{_start_time} = time();
	$self->{_size_done} = 0;
	($self->{_termwidth}, undef) = Term::Size::chars();
	return $self;
}

sub _termprint ($$) {
	my ($self, $string) = @_;

	if (length($string) > $self->{_termwidth}) {
		print substr($string, 0, $self->{_termwidth});
	} else {
		my $string_to_print = $string . (' ' x ($self->{_termwidth} - length($string)));
		print $string_to_print;
	}
}

sub __human_readable_speed ($) {
	my $bytes;
	return human_readable_size_string($bytes) . '/s';
}

sub progress {
	my ($self, $uri, $action, @params) = @_;

	# update info
	do {
		my $ref_entry;
		print "\r";
		if ($action eq 'start') {
			# new entry, create it
			$ref_entry = ($self->{_now_downloading}->{$uri} = {});
			$ref_entry->{number} = $self->{_next_download_number}++;
			# can be undef, be cautious
			$ref_entry->{size} = shift @params;
			$ref_entry->{downloaded} = 0;
			my $alias = $self->{_long_aliases}->{$uri} // $uri;
			my $size_suffix = defined $ref_entry->{size} ?
					" [" . human_readable_size_string($ref_entry->{size}) . "]" :
					"";
			$self->_termprint(sprintf "%s:%u %s%s", __("Get"), $ref_entry->{number}, $alias, $size_suffix);
		} else {
			# this is info about something that currently downloading
			$ref_entry = $self->{_now_downloading}->{$uri};
			given ($action) {
				when('downloading') {
					$ref_entry->{downloaded} = shift @params;

					state $prev_timestamp = time();
					my $timestamp = time();
					if ($timestamp != $prev_timestamp) {
						$prev_timestamp = $timestamp;
					} else {
						# don't renew stats too often just for download totals
						return;
					}
				}
				when ('expected-size') {
					$ref_entry->{size} = shift @params;
				}
				when('done') {
					my $result = shift @params;
					if ($result ne '0') {
						# some error occured, output it
						$self->_termprint(sprintf "error downloading %s: %s", $uri, $result);
					}
					$self->{_size_done} += $ref_entry->{size} // $ref_entry->{downloaded};
					delete $self->{_now_downloading}->{$uri};
				}
			}
		}
	};
	undef $uri;
	undef $action;

	# print 'em all!
	my @ref_entries_to_print;
	foreach my $uri (keys %{$self->{_now_downloading}}) {
		my $ref_entry;
		%{$ref_entry} = %{$self->{_now_downloading}->{$uri}};
		$ref_entry->{uri} = $uri;
		push @ref_entries_to_print, $ref_entry;
	}
	# sort by download numbers
	@ref_entries_to_print = sort { $a->{number} <=> $b->{number} } @ref_entries_to_print;

	my $whole_string = '';

	do { # calculating overall download percent
		# firstly, start up with filling size of already downloaded things
		my $total_downloaded_size = $self->{_size_done};
		# count each amount bytes download for all active entries
		$total_downloaded_size += (sum map { $_->{downloaded} } values %{$self->{_now_downloading}}) // 0;

		my $total_estimated_size;
	    if (defined $self->{_total_estimated_size}) {
			# if caller has specified the estimated size, just use it
			$total_estimated_size = $self->{_total_estimated_size};
		} else {
			# otherwise compute it based on data we have
			$total_estimated_size = $self->{_size_done};
			foreach my $ref_entry (values %{$self->{_now_downloading}}) {
				# add or real estimated size, or downloaded size (for entries
				# where download size hasn't been determined yet)
				$total_estimated_size += $ref_entry->{size} // $ref_entry->{downloaded};
			}
		}
		$whole_string .= sprintf "%.0f%% ", $total_downloaded_size / $total_estimated_size * 100;
	};

	foreach my $ref_entry (@ref_entries_to_print) {
		my $uri = $ref_entry->{uri};
		my $alias = $self->{_short_aliases}->{$uri} // $uri;
		my $size_substring = "";
		if (defined $ref_entry->{size}) {
			# filling size substring
			$size_substring = sprintf "/%s %.0f%%", human_readable_size_string($ref_entry->{size}),
					$ref_entry->{downloaded} / $ref_entry->{size} * 100;
		}
		$whole_string .= (sprintf "[%u %s %u%s]",
				$ref_entry->{number}, $alias, $ref_entry->{downloaded}, $size_substring);
	}
	$self->_termprint($whole_string);
}

sub __human_readable_difftime_string ($) {
	my ($time) = @_;

	my $days = int($time / 86400);
	$time -= ($days * 86400);
	my $hours = int($time / 3600);
	$time -= ($hours * 3600);
	my $minutes = int($time / 60);
	my $seconds = $time % 60;

	my $day_string = $days < 1 ? '' : $days .'d';
	my $hour_string = ($hours < 1 && length($day_string) == 0) ? '' : $hours .'h';
	my $minute_string = ($minutes < 1 && length($hour_string) == 0) ? '' : $minutes . 'm';

	return $day_string . $hour_string . $minute_string . $seconds . 's';
}

sub finish ($) {
	my ($self) = @_;

	print "\r";
	$self->_termprint(sprintf __("Fetched in %s."), __human_readable_difftime_string(time() - $self->{_start_time}));
}

1;

