use Test::More tests => 2;

sub test {
	my ($text, $expected_dep) = @_;

	my $cupt = setup('packages' =>
			[ compose_package_record('abc', 1) . "Depends: $expected_dep\n" ]);

	my $output = stdout("$cupt depends abc");
	like($output, qr/Depends: \Q$expected_dep\E\n/, $text);
}

test('versionless dependency', 'def:xyz');
test('versioned dependency', "klm:aaa (>= 1.2.3)");

