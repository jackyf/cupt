use Test::More tests => 17;

my $package = 'qqw';

sub compose_bd_record {
      my ($bd_line) = @_;
      return entail(compose_package_record($package, '0') . "Build-Depends: $bd_line\n");
}

sub test {
	my ($bd, $bd_parsed, $desc) = @_;

	my $cupt = setup('sources' => compose_bd_record($bd));

	my $output = stdall("$cupt showsrc $package");
	like($output, qr/^Build-Depends: \Q$bd_parsed\E/m, $desc);
}

test('aa', 'aa', 'bare');
test('aa  ', 'aa', 'spaces after package name');
test('sdf (>= 4.5)', 'sdf (>= 4.5)', 'simple relation');
test('sdf  (<<2)', 'sdf (<< 2)', 'whitespaces');
test('sdf1 (<< 2) | sdf2 (<< 3)', 'sdf1 (<< 2) | sdf2 (<< 3)', 'complex relation');
test('sdf [armb]', 'sdf [armb]', '1 positive architecture');
test('sdf  [ oi  ]', 'sdf [oi]', '1 positive architecture, whitespaces');
test('sdf [armb ppq]', 'sdf [armb ppq]', '2 positive architectures');
test('ter [!z1]', 'ter [!z1]', '1 negative architecture');
test('ter[ !z1   ]', 'ter [!z1]', '1 negative architecture, whitespaces');
test('ter [!z1 !z2]', 'ter [!z1 !z2', '2 negative architectures');
test('ter [!z1 !z2 !armb !ppq]', 'ter [!z1 !z2 !armb !ppq]', '4 negative architectures');
test('com (<< 5) [armb]', 'com (<< 5) [armb]', 'architecture, simple relation');
test('com(<<5)   [armb ]', 'com (<< 5) [armb]', 'architecture, simple relation, whitespaces');
test('com1 (= 1) | com2 | com3 (>> 3) [ppq]', 'com1 (= 1) | com2 | com3 (>> 3) [ppq]', 'architecture, complex relation');
test('com1 (= 1) | com2 [!oi]', 'com1 (= 1) | com2 [!oi]', 'negative architecture, complex relation');
test('com  (>> 5:0.0.2000  )[!oi1   !oi2 ]', 'com (>> 5:0.0.2000) [!oi1 !oi2]', '2 negative architectures, simple relation, whitespaces');

