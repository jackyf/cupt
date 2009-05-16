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

use warnings;
use strict;

use Exporter qw(import);

our @EXPORT_OK = qw(&get_acquire_suboption_for_uri);

use URI;

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

Returns: '' if all went smoothly, error string in case of error

=cut

sub perform ($$$$$) {
	# stub
}

=head1 FREE SUBROUTINES

=head2 get_acquire_suboption_for_uri

returns the value of some acquire suboption of 'acquire' group should be used
for supplied URI. This subroutine honors global and per-host settings.

Parameters:

I<config> - reference to L<Cupt::Config|Cupt::Config>

I<uri> - URI string

I<suboption_name> - suboption name

Example:

C<get_acquire_param_for_uri($config, $uri, 'proxy')>

=cut

sub get_acquire_suboption_for_uri ($$$) {
	my ($config, $uri, $suboption_name) = @_;

	my $uri_object = new URI($uri);
	my $protocol = $uri_object->scheme();
	my $host = $uri_object->host();
	# this "zoo" of per-host variants is given by APT...
	my $proxy = $config->var("acquire::${protocol}::${suboption_name}::${host}") //
			$config->var("acquire::${protocol}::${host}::${suboption_name}") //
			$config->var("acquire::${protocol}::${suboption_name}");
	return $proxy;
}

1;

