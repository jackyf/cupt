use Test::More tests => 34;

require(get_rinclude_path('pinning'));

my $scheme;
my $hostname;

sub test {
	my ($pin_expression, $expected_result) = @_;

	test_pinning(
		{
			'package' => 'qas',
			'version' => 1,
			'package_comment' => "scheme: $scheme, hostname: $hostname",
			'first_pin_line' => 'Package: *',
			'pin_expression' => $pin_expression,
			'release_properties' => {
				'scheme' => $scheme,
				'hostname' => $hostname,
			},
		},
		$expected_result
	);
}

$scheme = 'http';
foreach ('', '/packages', '/big/list/of/server/directories') {
	$hostname = 'ftp.eu.debian.org' . $_;
	test('origin "ftp.eu.debian.org"' => 1);
	test('origin "*.debian.org"' => 1);
	test('origin "*.knoppix.net"' => 0);
	test('origin "/org/"' => 1);
	test('origin "\'"' => 0);
	test('origin=*' => -1);
	test('origin abc' => 0);
	test('origi =abc' => -1);
}

$hostname = 'xx.yy.net/debian';
test('origin "/yy/"' => 1);
test('origin "/debian/"' => 0);

$hostname = 'home/user/repo';
foreach (qw(file copy)) {
	$scheme = $_;
	test('origin ""' => 1);
	test('origin "*home*"' => 0);
	test('origin "/./"' => 0);
}

$scheme = 'ftp';
test('origin ""' => 0);
test('origin "*home*"' => 1);

