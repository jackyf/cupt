#***************************************************************************
#*   Copyright (C) 2009 by Eugene V. Lyubimkin                        *
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
package Cupt::Download::Methods::Debdelta;

=head1 NAME

Cupt::Download::Methods::File - download through debdelta method for Cupt

=head1 ABSTRACT

Used for handling debdelta:// URIs (which are produced automatically).

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

	my $delta_expected_size;

	my $sub_delta_callback = sub {
		my ($type, @params) = @_;

		if ($type eq 'expected-size') {
			$delta_expected_size = $params[0];
		} else {
			$sub_callback->($type, @params);
		}
	};

	# download delta file
	my $delta_uri = URI->new($uri)->file();
	my $delta_download_method = new Cupt::Download::Method::choose($debdelta_uri);
	my $delta_download_filename = "$filename.delta";
	my $delta_download_result = $delta_file_download_method->new()->
			perform($config, $delta_uri, $delta_download_filename, $sub_delta_callback);
	if ($delta_download_result) {
		return sprintf "delta download failed: %s", $delta_download_result;
	}

	# invoking a deb patcher
	my $patch_result = system("debpatch $delta_download_filename / $filename");

	# all went ok
	return '';
}

1;

