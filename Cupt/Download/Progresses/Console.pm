package Cupt::Download::Progresses::Console;

use base Cupt::Download::Progress;

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
			print __("Get") . ":" . $ref_entry->{number} . " " . $uri . "\n";
		}

		given ($action) {
			when('downloading') {
				$ref_entry->{downloaded} = shift @params;
			}
		}
	}
	undef $uri;
	undef $action;

	# print 'em all!
	my @ref_entries_to_print;
	foreach my $uri (keys $self->{_now_downloading}) {
		my $ref_entry;
		%{$ref_entry} = %{$self->{_now_downloading}->{$uri}};
		$ref_entry->{uri} = $uri;
		push @entries_to_print, $ref_entry;
	}
	# sort by download numbers
	@entries_to_print = sort { $a->{number} <=> $b->{number} } @ref_entries_to_print;

	foreach my $ref_entry (@entries_to_print) {
		print "[";
		print $ref_entry->{number}
		print "]";
	}

	print "@params\n";
}

1;

