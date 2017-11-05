use Test::More tests => 6;

sub test {
	my ($ma_value, $expected_count) = @_;

	my $ma_stanza = defined($ma_value) ? "Multi-Arch: $ma_value\n" : '';
	my $cupt = setup('packages' => [ compose_package_record('abc', 0) . $ma_stanza ]); 

	my $output = get_all_offers("$cupt satisfy 'abc:native'");
	is(get_offer_count($output), $expected_count, $ma_stanza) or diag($output);
}

test(undef, 0);
test('corruptedfoobla', 0);
test('no', 0);
test('same', 0);
test('foreign', 1);
test('allowed', 0);

