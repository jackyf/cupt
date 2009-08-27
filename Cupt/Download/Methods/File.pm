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
package Cupt::Download::Methods::File;

=head1 NAME

Cupt::Download::Methods::File - local file download method for Cupt

=head1 ABSTRACT

Used for handling file:// URIs.

=cut

use strict;
use warnings;
use 5.10.0;

use base qw(Cupt::Download::Method);

use URI;
use File::stat;

use Cupt::Core;

sub perform ($$$$$) {
	my ($self, $config, $uri, $filename, $sub_callback) = @_;

	my $source_filename = URI->new($uri)->file();
	my $scheme = URI->new($uri)->scheme();

	if ($scheme eq 'copy') {
		# full copying

		# preparing target
		open(my $fd, '>>', $filename) or
				return sprintf "unable to open file '%s' for appending: %s", $filename, $!;
		my $total_bytes = tell($fd);
		$sub_callback->('downloading', $total_bytes, 0);

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
			print { $fd } $chunk or
					return sprintf "unable to write to file '%s': %s", $filename, $!;

			my $written_bytes = length($chunk);
			$total_bytes += $written_bytes;
			$sub_callback->('downloading', $total_bytes, $written_bytes);
		};

		close($fd) or
				mydie("unable to close file '%s': %s", $filename, $!);
	} elsif ($scheme eq 'file') {
		# symlinking
		unlink $filename; ## no critic (RequireCheckedSyscalls)
		my $result = symlink $source_filename, $filename;
		if (!$result) {
			mydie("unable to create symbolic link '%s' -> '%s': %s",
					$filename, $source_filename, $!);
		}
	} else {
		myinternaldie('wrong scheme for File method');
	}

	# all went ok
	return '';
}

1;

