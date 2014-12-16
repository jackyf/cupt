use TestCupt;
use Test::More tests => 3;

use strict;
use warnings;

my $past_date = 'Mon, 07 Oct 2013 14:44:53 UTC';

my $cupt = TestCupt::setup(
	'packages2' =>
		[
			{
				'archive' => 'aaa',
				'valid-until' => 'Tue, 01 Jan 2030 00:00:00 UTC',
				'content' => '',
			},
			{
				'archive' => 'bbb',
				'valid-until' => $past_date,
				'content' => '',
			}
		]
);

my $output = stdall("$cupt policy");

like($output, qr/a=aaa/, "release with 'valid-until' in the future is valid");

subtest "release with 'valid-until' date in the past is invalid by default" => sub {
	unlike($output, qr/a=bbb/, "not present in the release list");
	like($output, qr/^E: the release '.* bbb' has expired/, 'warning is printed');
	like($output, qr/\Qhas expired (expiry time '$past_date')\E/, 'expiry date is printed');
};


$output = stdall("$cupt policy -o cupt::cache::release-file-expiration::ignore=yes");

like($output, qr/a=bbb/, "release with 'valid-until' date in the past is valid if expiration check is off");

