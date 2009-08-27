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
package Cupt::Download::Progress;

=head1 NAME

Cupt::Download::Progress - base class for download progess meters

=cut

use 5.10.0;
use strict;
use warnings;

use List::Util qw(sum max);
use Time::HiRes qw(tv_interval gettimeofday);

use Cupt::Core;

=head1 PARAMS

=head2 o_download_speed_accuracy

specifies the number of milliseconds that will be used for speed counting. The
bigger value means more smooth speed changes. Defaults to 4000.

=cut

our $o_download_speed_accuracy = 4000;

=head1 METHODS

=head2 new

creates new Cupt::Download::Progress object; should be called by subclass
creating methods before all other actions

=cut

sub new {
	my $class = shift;
	my $self = {};
	$self->{_long_aliases} = {};
	$self->{_short_aliases} = {};
	$self->{_total_estimated_size} = undef;
	$self->{_now_downloading} = {};
	$self->{_next_download_number} = 1;
	$self->{_start_time} = time();
	$self->{_size_done} = 0;
	$self->{_downloaded} = 0;
	# for download speed counting
	$self->{_size_changes} = [];
	$self->{_uncounted_float_seconds} = 0;
    return bless $self => $class;
}

=head2 get_start_time

method, returns UNIX timestamp when downloads started

=cut

sub get_start_time ($) {
	my ($self) = @_;
	return $self->{_start_time};
}

=head2 set_long_alias_for_uri

method, sets long alias for uri to show

Parameters:

I<uri> - URI

I<alias> - long alias string

=cut

sub set_long_alias_for_uri ($$$) {
	my ($self, $uri, $alias) = @_;
	$self->{_long_aliases}->{$uri} = $alias;
	return;
}

=head2 get_long_alias_for_uri

method, gets long alias for uri

Parameters:

I<uri> - URI

Returns:

I<alias> - long alias string, undef if not set

=cut

sub get_long_alias_for_uri ($$) {
	my ($self, $uri) = @_;
	return $self->{_long_aliases}->{$uri};
}

=head2 set_short_alias_for_uri

method, sets short alias for uri to show

Parameters:

I<uri> - URI

I<alias> - short alias

=cut

sub set_short_alias_for_uri ($$$) {
	my ($self, $uri, $alias) = @_;
	$self->{_short_aliases}->{$uri} = $alias;
	return;
}

=head2 get_short_alias_for_uri

method, gets short alias for uri

Parameters:

I<uri> - URI

Returns:

I<alias> - short alias string, undef if not set

=cut

sub get_short_alias_for_uri ($$) {
	my ($self, $uri) = @_;
	return $self->{_short_aliases}->{$uri};
}

=head2 set_total_estimated_size

method, set estimated total size of downloads

Parameters:

I<total_size> - total estimated size in bytes

=cut

sub set_total_estimated_size ($$) {
	my ($self, $size) = @_;
	$self->{_total_estimated_size} = $size;
	return;
}

=head2 progress

this method is called everytime something changed within downloading process

Parameters:

I<uri> - URI of the download

Next parameters are the same as specified for the callback function for the
'perform' method of the L<Cupt::Download::Method|Cupt::Download::Method> class, consult its
documentation.

Exceptions:

=over

=item *

'start' - message turns download start

I<size> - size in bytes of the download, can be skipped if it's unknown before
the download

=item *

'done' - message turns download finish

I<result> - 0 if success, error string in case of error

=item *

'ping' - update progress view

=back

=cut

sub progress ($$$;@) {
	my ($self, $uri, $action, @params) = @_;

	my $ref_entry;
	if ($action eq 'ping') {
		$self->hook('ping', 0);
	} elsif ($action eq 'start') {
		# new entry, create it
		$ref_entry = ($self->{_now_downloading}->{$uri} = {});
		$ref_entry->{'number'} = $self->{_next_download_number}++;
		# can be undef, be cautious
		$ref_entry->{'size'} = shift @params;
		$ref_entry->{'downloaded'} = 0;
		$self->hook('start', $uri);
		$self->hook('ping', 1);
	} else {
		# this is info about something that currently downloading
		$ref_entry = $self->{_now_downloading}->{$uri};
		defined $ref_entry or myinternaldie("received info for not started download for uri '$uri'");
		given ($action) {
			when('downloading') {
				$ref_entry->{'downloaded'} = shift @params;
				my $bytes_in_fetched_piece = shift @params;
				$self->{_downloaded} += $bytes_in_fetched_piece;
				push @{$self->{_size_changes}}, [ [gettimeofday()], $bytes_in_fetched_piece ];
				$self->hook('ping', 0);
			}
			when ('expected-size') {
				$ref_entry->{'size'} = shift @params;
				$self->hook('ping', 1);
			}
			when ('done') {
				my $result = shift @params;
				if (!$result) {
					# only if download succeeded
					$self->{_size_done} += $ref_entry->{'size'} // $ref_entry->{'downloaded'};
				}
				$self->hook('done', $uri, $result);
				delete $self->{_now_downloading}->{$uri};
				$self->hook('ping', 1);
			}
			default {
				myinternaldie("download progress: wrong action '$action' received");
			}
		}
	}
	return;
}

=head2 hook

method which should be re-implemented by subclasses to do some useful things
when progress message appeared

Parameters:

=over

=item *

'start', $uri - $uri just started downloading

=item *

'done', $uri, $result - $uri just finished downloading, result is $result

=item *

'ping', $update_required - the signal to renew user-visible download progress, if $update_required is set to false, the subclass may choose whether it wants to update the progress or not

=back

=cut

sub hook ($) {
	# stub
}

=head2 get_overall_download_percent

method, returns float percent value of what part of all downloads are done

=cut

sub get_overall_download_percent ($) {
	my ($self) = @_;

	# firstly, start up with filling size of already downloaded things
	my $total_downloaded_size = $self->{_size_done};
	# count each amount bytes download for all active entries
	$total_downloaded_size += (sum map { $_->{'downloaded'} } values %{$self->{_now_downloading}}) // 0;

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
			$total_estimated_size += $ref_entry->{'size'} // $ref_entry->{'downloaded'};
		}
	}
	if ($total_estimated_size == 0) {
		return 0;
	} else {
		return $total_downloaded_size / $total_estimated_size * 100;
	}
}

=head2 get_overall_downloaded_size

method, returns the number of bytes that were fetched

=cut

sub get_overall_downloaded_size ($) {
	my ($self) = @_;

	return $self->{_downloaded};
}

=head2 get_overall_estimated_time

method, returns estimated time (in seconds) to complete the downloads

=cut

sub get_overall_estimated_time ($) {
	my ($self) = @_;

	my $overall_part = max($self->get_overall_download_percent(), 0.01) / 100;
	my $current_time = time();
	return ($current_time - $self->get_start_time()) / $overall_part * (1 - $overall_part);
}

=head2 download_entries

mutator, returns download entry as hash { $uri => I<record> }

where I<record> is hash { 'number' => I<number>, 'size' => I<size>, 'downloaded' => I<downloaded>' }

I<number> - number of the download (starting with 1)

I<size> - estimated size of the download (can be undef)

I<downloaded> - already downloaded bytes count

Subclasses are free to add own fields to returned hash entry, newly added pairs
key->value will be preserved.

=cut

sub download_entries ($$) {
	my ($self) = @_;
	return $self->{_now_downloading};
}

sub _update_size_changes ($) {
	my ($self) = @_;

	my $now = [gettimeofday()];
	my $float_time_accuracy = $o_download_speed_accuracy / 1000;
	my $exit_flag = 0;
	while (!$exit_flag) {
		my $piece = shift @{$self->{_size_changes}};
		last if !defined $piece;

		my ($piece_time, $piece_size) = @$piece;
		my $interval = tv_interval($piece_time, $now);
		if ($interval < $float_time_accuracy) {
			# all further times will be even near to now
			unshift @{$self->{_size_changes}}, $piece;
			$exit_flag = 1;
		} else {
			$self->{_uncounted_float_seconds} = $interval - $float_time_accuracy;
		}
	}
	return;
}

=head2 get_download_speed

method, returns current download speed in bytes/seconds

=cut

sub get_download_speed ($) {
	my ($self) = @_;

	$self->_update_size_changes();
	my $earliest_piece_time_correction;
	if (scalar @{$self->{_size_changes}}) {
		my $earliest_piece_time = $self->{_size_changes}->[-1]->[0];
		$earliest_piece_time_correction = tv_interval($earliest_piece_time);
	} else {
		$earliest_piece_time_correction = 0;
	}
	my $bytes = 0;
	foreach (@{$self->{_size_changes}}) {
		$bytes += $_->[1];
	}

	my $counted_time = $o_download_speed_accuracy / 1000 +
			$self->{_uncounted_float_seconds} - $earliest_piece_time_correction;
	return $bytes / $counted_time;
}

=head2 finish

this method is called when all downloads are done

=cut

sub finish ($) {
	# stub
}

1;

