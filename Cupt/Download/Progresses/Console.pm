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
package Cupt::Download::Progresses::Console;

=head1 NAME

Cupt::Download::Progresses::Console - console download progress for Cupt

=cut

use 5.10.0;
use warnings;
use strict;

use base qw(Cupt::Download::Progress);

use Term::Size;

use Cupt::Core;

sub new {
	my $class = shift;
	my $self = $class->SUPER::new();
	$self->{_previous_report_time} = 0;
	($self->{_termwidth}, undef) = Term::Size::chars();
	return $self;
}

sub _termprint ($$$) {
	my ($self, $string, $right_appendage) = @_;

	# enable flushing STDOUT
	local $| = 1;

	$right_appendage //= '';
	my $allowed_width = $self->{_termwidth} - length($right_appendage);
	print "\r";
	if (length($string) > $allowed_width) {
		print substr($string, 0, $allowed_width);
	} else {
		my $string_to_print = $string . (' ' x ($allowed_width - length($string)));
		print $string_to_print;
	}
	print $right_appendage;
	return;
}

sub __human_readable_speed ($) {
	my ($bytes) = @_;
	return human_readable_size_string($bytes) . '/s';
}

sub hook {
	my ($self, $message, @params) = @_;

	# update info
	given ($message) {
		when ('start') {
			my $uri = shift @params;
			my $ref_entry = $self->download_entries->{$uri};

			my $alias = $self->get_long_alias_for_uri($uri) // $uri;
			my $size_suffix = defined $ref_entry->{'size'} ?
					' [' . human_readable_size_string($ref_entry->{'size'}) . ']' :
					'';
			$self->_termprint(sprintf '%s:%u %s%s', __('Get'), $ref_entry->{'number'}, $alias, $size_suffix);
			print "\n";
		}
		when ('done') {
			my $uri = shift @params;
			my $ref_entry = $self->download_entries->{$uri};
			my $error_string = shift @params;
			if ($error_string) {
				# some error occured, output it
				mywarn('downloading %s failed: %s', $uri, $error_string);
			}
		}
		when ('ping') {
			my $update_required = shift @params;
			if (!$update_required) {
				my $timestamp = time();
				if ($timestamp != $self->{_previous_report_time}) {
					$self->{_previous_report_time} = $timestamp;
				} else {
					# don't renew stats too often just for download totals
					return;
				}
			}

			# don't print progress meter when not connected to a TTY
			-t STDOUT or return;

			# print 'em all!
			my @ref_entries_to_print;
			foreach my $uri (keys %{$self->download_entries}) {
				my $ref_entry;
				%{$ref_entry} = %{$self->download_entries->{$uri}};
				$ref_entry->{'uri'} = $uri;
				push @ref_entries_to_print, $ref_entry;
			}
			# sort by download numbers
			@ref_entries_to_print = sort { $a->{'number'} <=> $b->{'number'} } @ref_entries_to_print;

			my $whole_string .= sprintf '%.0f%% ', $self->get_overall_download_percent();

			foreach my $ref_entry (@ref_entries_to_print) {
				my $uri = $ref_entry->{'uri'};
				my $alias = $self->get_short_alias_for_uri($uri) // $uri;
				my $size_substring = '';
				if (defined $ref_entry->{'size'}) {
					# filling size substring
					$size_substring = sprintf '/%s %.0f%%', human_readable_size_string($ref_entry->{'size'}),
							$ref_entry->{'downloaded'} / $ref_entry->{'size'} * 100;
				}
				$whole_string .= sprintf('[%u %s %s%s]', $ref_entry->{'number'}, $alias,
						human_readable_size_string($ref_entry->{'downloaded'}), $size_substring);
			}
			my $speed_and_time_appendage = sprintf '| %s | ETA: %s',
					__human_readable_speed($self->get_download_speed()),
					__human_readable_difftime_string($self->get_overall_estimated_time());
			$self->_termprint($whole_string, $speed_and_time_appendage);
		}
	}
	return;
}

sub __human_readable_difftime_string ($) {
	my ($time) = @_;

	my $days = int($time / 86_400);
	$time -= ($days * 86_400);
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

	$self->_termprint(sprintf __('Fetched %s in %s.'),
			human_readable_size_string($self->get_overall_downloaded_size()),
			__human_readable_difftime_string(time() - $self->get_start_time()));
	print "\n";
	return;
}

1;

