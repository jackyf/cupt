package Cupt::Download::Methods::Curl

use strict;
use warnings;
use 5.10.0;

use fields qw(_config _curl_handle _filename);

use Cupt::Core;
use WWW::Curl::Easy 4.05;
use WWW::Curl::Share;

our $_curl_share_handle = new WWW::Curl::Share;
$_curl_share_handle->setopt(CURLOPT_SHARE, CURL_LOCK_DATA_COOKIE);
$_curl_share_handle->setopt(CURLOPT_SHARE, CURL_LOCK_DATA_DNS);
# those two are undocumented somewhy, but potentially useful ones
$_curl_share_handle->setopt(CURLOPT_SHARE, CURL_LOCK_DATA_CONNECT);
$_curl_share_handle->setopt(CURLOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);

sub new {
	my ($class, $config, $uri, $filename) = shift;
	my $self = fields::new($class);
	$self->{_config} = $config;
	$self->{_uri} = $uri;
	$self->{_filename} = $filename;
	return $self;
}

sub perform ($) {
	my ($self)

	my $curl = new WWW::Curl::Easy;
	open(my $fd, '>>', $self->{_filename});

	my $total_bytes = tell($fd);

	my $sub_writefunction = sub {
		print { $_[1] } $_[0];
		my $written_bytes = length($_[0]);
		print "Written bytes: $written_bytes, total bytes: $total_bytes\n";
		$total_bytes += $written_bytes;
		return $written_bytes;
	}

	my ($protocol) = ($self->{_uri} =~ m{(\w+)::/});

	$curl->setopt(CURLOPT_URL, $self->{_uri});
	$curl->setopt(CURLOPT_MAX_RECV_SPEED_LARGE, $self->{_config}->var("acquire::${protocol}::dl-limit");
	$curl->setopt(CURLOPT_WRITEFUNCTION, \&writefunction);
	$curl->setopt(CURLOPT_WRITEDATA, $fd);
	$curl->setopt(CURLOPT_RESUME_FROM, tell($fd));
	$curl->perform();
}

