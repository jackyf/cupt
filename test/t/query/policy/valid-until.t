use Test::More tests => 5;

sub get_output {
	my ($vu, $arguments) = @_;

	my $cupt = setup(
		'releases' => [
			{
				'archive' => 'aaa',
				'valid-until' => $vu,
				'packages' => [],
			},
		]
	);

	return stdall("$cupt policy $arguments");
}

my $future_date = 'Tue, 01 Jan 2030 00:00:00 UTC';
my $past_date = 'Mon, 07 Oct 2013 14:44:53 UTC';
my $corrupted_date = '#%(&Y(&9';
my $presency_regex = qr/a=aaa/;
my $expiry_regex = qr/the release '.* aaa' has expired/;

subtest "empty 'valid-until' is okay" => sub {
	my $output = get_output('', '');
	like($output, $presency_regex, 'is present');
	unlike($output, qr/^E:/, 'no errors');
	unlike($output, qr/^W:/, 'no warnings');
};

subtest "release with 'valid-until' in the future is valid" => sub {
	my $output = get_output($future_date, '');
	like($output, $presency_regex);
};

subtest "release with 'valid-until' date in the past is invalid by default" => sub {
	my $output = get_output($past_date, '');
	unlike($output, $presency_regex, "not present in the release list");
	like($output, qr/^E: $expiry_regex/, 'error is printed');
	like($output, qr/\Qhas expired (expiry time '$past_date')\E/, 'expiry date is printed');
};

subtest "non-parseable 'valid-until' date" => sub {
	my $output = get_output($corrupted_date, '');
	like($output, $presency_regex, 'treated as valid');
	like($output, qr/^\QW: unable to parse the expiry time '$corrupted_date' in the release \E'.* aaa'$/m, 'warning is printed');
};

subtest 'expiration check globally suppressed' => sub {
	my $output = get_output($past_date, '-o cupt::cache::release-file-expiration::ignore=yes');
	like($output, $presency_regex, 'is present');
	like($output, qr/^W: $expiry_regex/, 'warning is printed');
	like($output, qr/\Qhas expired (expiry time '$past_date')\E/, 'expiry date is printed');
};

