package Cupt::Download::Methods::Curl;

use strict;
use warnings;
use 5.10.0;

use base qw(Cupt::Download::Method);

use Cupt::Core;
use WWW::Curl::Easy 4.05;
use WWW::Curl::Share;

our $_curl_share_handle = new WWW::Curl::Share;
$_curl_share_handle->setopt(CURLOPT_SHARE, CURL_LOCK_DATA_COOKIE);
$_curl_share_handle->setopt(CURLOPT_SHARE, CURL_LOCK_DATA_DNS);
# those two are undocumented somewhy, but potentially useful ones
$_curl_share_handle->setopt(CURLOPT_SHARE, CURL_LOCK_DATA_CONNECT);
$_curl_share_handle->setopt(CURLOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);

sub perform ($$$$$) {
	my ($self, $config, $uri, $filename, $sub_callback) = shift;

	my $curl = new WWW::Curl::Easy;
	open(my $fd, '>>', $filename);

	my $total_bytes = tell($fd);

	my $sub_writefunction = sub {
		# writing data to file
		print $fd $_[0];

		my $written_bytes = length($_[0]);
		$total_bytes += $written_bytes;
		$sub_callback->('downloading', $total_bytes);

		return $written_bytes;
	};

	my ($protocol) = ($uri =~ m{(\w+)::/});

	$curl->setopt(CURLOPT_URL, $uri);
	$curl->setopt(CURLOPT_MAX_RECV_SPEED_LARGE, $config->var("acquire::${protocol}::dl-limit"));
	$curl->setopt(CURLOPT_WRITEFUNCTION, \&writefunction);
	# FIXME: replace 1 with CURL_NETRC_OPTIONAL after libwww-curl is advanced to provide it
	$curl->setopt(CURLOPT_NETRC, 1);
	$curl->setopt(CURLOPT_RESUME_FROM, tell($fd));
	return $curl->perform();
}

1;

