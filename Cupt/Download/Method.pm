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
package Cupt::Download::Method;

=head1 NAME

Cupt::Download::Method - base class for all Cupt download methods

=head1 ABSTRACT

It should be never instantiated directly. If you want to write your own
download method for Cupt, you are need to implement 'perform' method specified
under.

=head1 METHODS

=head2 new

creates new Cupt::Download::Method object

=cut

sub new {
	my $class = shift;
	return bless {} => $class;
}

=head2 perform

downloads specified file

Parameters:

I<config> - reference to Cupt::Config

I<uri> - string that determines URL to download

I<filename> - target file name

I<sub_callback> - callback subroutine to report status change of download

=over

Allowed param sets:

=over

=item *

"connecting", "$ip[:$port]"

=item *

"expected-size", I<expected_size>

=item *

"downloading" - I<total_downloaded_bytes> I<fetched_bytes>;

=back

where:

I<expected_size> - size of file to download

I<total_downloaded_bytes> - number of bytes done for download; in case of resumed
download this number should include already downloaded size

I<fetched_bytes> - number of bytes that were really fetched since the last callback call

=back

Returns: 0 if all went smoothly, error string in case of error

=cut

sub perform ($$$$$) {
	# stub
}

1;

