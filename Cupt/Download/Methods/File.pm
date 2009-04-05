package Cupt::Download::Methods::File;

use strict;
use warnings;
use 5.10.0;

use base qw(Cupt::Download::Method);

use URI;
use File::stat;

use Cupt::Core;

sub perform ($$$$$) {
	my ($self, $config, $uri, $filename, $sub_callback) = @_;

	# preparing target
	open(my $fd, '>>', $filename) or
			return sprintf "unable to open file '%s' for appending: %s", $filename, $!;
	my $total_bytes = tell($fd);
	$sub_callback->('downloading', $total_bytes);
	
	# checking and preparing target
	my $source_filename = URI->new($uri)->file();
	open(my $source_fd, '<', $source_filename) or
			return sprintf "unable to open file '%s' for reading: %s", $source_filename, $!;

	my $stat = stat($source_fd) or
			return sprintf "unable to stat file '%s': %s", $source_filename, $!;
	$sub_callback->('expected-size', $stat->size());

	# writing
	my $chunk;
	my $block_size = 4096;
	while (sysread $source_fd, $chunk, $block_size) {
		# writing data to file
		print $fd $chunk or
				return sprintf "unable to write to file '%s': %s", $filename, $!;

		my $written_bytes = length($chunk);
		$total_bytes += $written_bytes;
		$sub_callback->('downloading', $total_bytes);
	};

	close($fd) or
			mydie("unable to close file '%s': %s", $filename, $!);

	# all went ok
	return 0;
}

1;

