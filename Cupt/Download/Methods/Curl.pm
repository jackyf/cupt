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
package Cupt::Download::Methods::Curl;

=head1 NAME

Cupt::Download::Methods::Curl - download method for Cupt using libwww-curl

=head1 ABSTRACT

May be used for handling http, https, ftp schemes.

=cut

use strict;
use warnings;
use 5.10.0;

use base qw(Cupt::Download::Method);

use WWW::Curl::Easy 4.05;
use WWW::Curl::Share;
use URI;

use Cupt::Core;
use Cupt::Download::Method qw(get_acquire_suboption_for_uri);

our $_curl_share_handle = new WWW::Curl::Share;
$_curl_share_handle->setopt(CURLOPT_SHARE, CURL_LOCK_DATA_COOKIE);
$_curl_share_handle->setopt(CURLOPT_SHARE, CURL_LOCK_DATA_DNS);
# those two are undocumented somewhy, but potentially useful ones
$_curl_share_handle->setopt(CURLOPT_SHARE, CURL_LOCK_DATA_CONNECT);
$_curl_share_handle->setopt(CURLOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);

sub perform ($$$$$) {
	my ($self, $config, $uri, $filename, $sub_callback) = @_;

	my $curl = new WWW::Curl::Easy;
	open(my $fd, '>>', $filename) or
			return sprintf "unable to open file '%s': %s", $filename, $!;

	my $total_bytes = tell($fd);
	$sub_callback->('downloading', $total_bytes, 0);

	my $is_expected_size_reported = 0;

	my $write_error;

	my $sub_writefunction = sub {
		# writing data to file
		print $fd $_[0] or
				do { $write_error = $!; return 0; };

		if (!$is_expected_size_reported) {
			$sub_callback->('expected-size', $curl->getinfo(CURLINFO_CONTENT_LENGTH_DOWNLOAD) + $total_bytes);
			$is_expected_size_reported = 1;
		}

		my $written_bytes = length($_[0]);
		$total_bytes += $written_bytes;
		$sub_callback->('downloading', $total_bytes, $written_bytes);

		return $written_bytes;
	};

	my $protocol = URI->new($uri)->scheme();

	$curl->setopt(CURLOPT_FAILONERROR, 1);
	$curl->setopt(CURLOPT_URL, $uri);
	my $download_limit = get_acquire_suboption_for_uri($config, $uri, 'dl-limit');
	$curl->setopt(CURLOPT_MAX_RECV_SPEED_LARGE, $download_limit*1024) if defined $download_limit;
	my $proxy = get_acquire_suboption_for_uri($config, $uri, 'proxy');
	$curl->setopt(CURLOPT_PROXY, $proxy) if defined $proxy;
	if ($protocol eq 'http') {
		$curl->setopt(CURLOPT_FOLLOWLOCATION, 1) if $config->var('acquire::http::allow-redirects');
	}
	my $timeout = get_acquire_suboption_for_uri($config, $uri, 'timeout');
	if (defined $timeout) {
		$curl->setopt(CURLOPT_CONNECTTIMEOUT, $timeout);
		$curl->setopt(CURLOPT_LOW_SPEED_LIMIT, 1);
		$curl->setopt(CURLOPT_LOW_SPEED_TIME, $timeout);
	}
	$curl->setopt(CURLOPT_WRITEFUNCTION, $sub_writefunction);
	# FIXME: replace 1 with CURL_NETRC_OPTIONAL after libwww-curl is advanced to provide it
	$curl->setopt(CURLOPT_NETRC, 1);
	$curl->setopt(CURLOPT_RESUME_FROM, tell($fd));

	my $curl_result = $curl->perform();

	close($fd) or
			return sprintf "unable to close file '%s': %s", $filename, $!;

	if (defined $write_error) {
		return sprintf "unable to write to file '%s': %s", $filename, $write_error;
	} elsif ($curl_result == 0) {
		# all went ok
		return '';
	# FIXME: replace 18 with CURLE_PARTIAL_FILE after libwww-curl is advanced to provide it
	} elsif ($curl_result == 18) {
		# partial data? no problem, we might request it
		return '';
	} else {
		# something went wrong
		my $result = $curl->strerror($curl_result);
		# some http/https/ftp error, provide an error code
		if ($curl_result == 22) {
			$result .= sprintf ": %u", $curl->getinfo(CURLINFO_RESPONSE_CODE);
		}
		return $result;
	}
}

1;

