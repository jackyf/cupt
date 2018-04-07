use Test::More tests => 5;

require(get_rinclude_path('common'));

sub test {
	my ($status, $reason_regex) = @_;

	my $record = compose_installed_record('abc', 1, 'status-line' => "install ok $status");
	my $cupt = setup('dpkg_status' => [ $record ]);

	my $rc = get_reason_chain($cupt, '', 'abc');
	like($rc, $reason_regex, "auto-configuring | $status");
}

my $configure_reason = qr/^abc: implicit: configuring partially installed packages$/;
my $no_reason = qr/extraction failed.*nothing to do/is;

test('unpacked', $configure_reason);
test('half-configured', $configure_reason);
test('triggers-pending', $configure_reason);
test('triggers-awaited', $no_reason);
test('installed', $no_reason);

