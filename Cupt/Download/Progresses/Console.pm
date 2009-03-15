package Cupt::Download::Progresses::Console;

use 5.10.0;
use warnings;
use strict;

use base qw(Cupt::Download::Progress);

use Cupt::Core;

sub new {
	my $class = shift;
	my $self = $class->SUPER::new();
	$self->{_now_downloading} = {};
	$self->{_next_download_number} = 1;
	return $self;
}

sub progress {
	my ($self, $uri, $action, @params) = @_;

	# update info
	do {
		my $ref_entry;
		print "\r";
		if (exists $self->{_now_downloading}->{$uri}) {
			# this is info about something that currently downloading
			$ref_entry = $self->{_now_downloading}->{$uri};
		} else {
			# new entry, create it
			$ref_entry = ($self->{_now_downloading}->{$uri} = {});
			$ref_entry->{number} = $self->{_next_download_number}++;
			$ref_entry->{size} = undef;
			$ref_entry->{downloaded} = 0;
			my $alias = $self->{_long_aliases}->{$uri} // $uri;
			print sprintf "%s:%u %s\n", __("Get"), $ref_entry->{number}, $alias;
		}

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

	foreach my $ref_entry (@ref_entries_to_print) {
		my $uri = $ref_entry->{uri};
		my $alias = $self->{_short_aliases}->{$uri} // $uri;
		my $size_substring = "";
		if (defined $ref_entry->{size}) {
			# filling size substring
			$size_substring = sprintf "/%s %.0f%%", human_readable_size_string($ref_entry->{size}),
					$ref_entry->{downloaded} / $ref_entry->{size} * 100;
		}
		print sprintf "[%u %s %u%s]", $ref_entry->{number}, $alias, $ref_entry->{downloaded}, $size_substring;
	}
}

1;

