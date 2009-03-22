package Cupt::Download::Progresses::Console;

use 5.10.0;
use warnings;
use strict;

use base qw(Cupt::Download::Progress);

use Term::Size;

use Cupt::Core;

sub new {
	my $class = shift;
	my $self = $class->SUPER::new();
	$self->{_now_downloading} = {};
	$self->{_next_download_number} = 1;
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

	my $whole_string;
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

1;

