use Test::More tests => 4;

my $past_date = 'Mon, 07 Oct 2013 14:44:53 UTC';
my $corrupted_date = '#%(&Y(&9';

my $cupt = TestCupt::setup(
	'releases' =>
		[
			{
				'archive' => 'aaa',
				'valid-until' => 'Tue, 01 Jan 2030 00:00:00 UTC',
				'packages' => [],
			},
			{
				'archive' => 'bbb',
				'valid-until' => $past_date,
				'packages' => [],
			},
			{
				'archive' => 'ccc',
				'valid-until' => $corrupted_date,
				'packages' => [],
			},
		]
);

my $output = stdall("$cupt policy");

like($output, qr/a=aaa/, "release with 'valid-until' in the future is valid");

subtest "release with 'valid-until' date in the past is invalid by default" => sub {
	unlike($output, qr/a=bbb/, "not present in the release list");
	like($output, qr/^E: the release '.* bbb' has expired/, 'warning is printed');
	like($output, qr/\Qhas expired (expiry time '$past_date')\E/, 'expiry date is printed');
};

like($output, qr/^\QW: unable to parse the expiry time '$corrupted_date'\E/m,
		"warning is printed for non-parseable 'valid-until' date");


$output = stdall("$cupt policy -o cupt::cache::release-file-expiration::ignore=yes");

like($output, qr/a=bbb/, "release with 'valid-until' date in the past is valid if expiration check is off");

