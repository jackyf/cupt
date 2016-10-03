use Test::More tests => 3 + 5;

my $package = 'qqw';

sub compose_bd_record {
      my ($bd_line) = @_;
      return entail(compose_package_record($package, '0') . "Build-Depends: $bd_line\n");
}

sub test {
	my ($bd, $area, $broken_part, $desc) = @_;

	my $cupt = setup('sources' => compose_bd_record($bd));

	my $output = stdall("$cupt showsrc $package");
	like($output, qr/^E: unable to parse $area \Q'$broken_part'\E/m, $desc);
}

sub test_af {
	my ($bd, $broken_part, $desc) = @_;
	test($bd, 'architecture filters', $broken_part, $desc);
}

sub test_bp {
	my ($bd, $broken_part, $desc) = @_;
	test($bd, 'build profiles', $broken_part, $desc);
}

test_af('sdf [armb ', '[armb ', 'unclosed list of architectures');
test_af('sdf  [', '[', 'no architectures');
test_af('sdf [armb, ppq]', '[armb', 'wrong architecture delimiter');

test_bp('rrr <noc', '<noc', 'unclosed profile list');
test_bp('rrr  <', '<', 'no profiles');
test_bp('rrr <noc, nod>', '<noc', 'wrong profile delimiter');
test_bp('rrr <noc> <m', '<m', 'unclosed second profile list');
test_bp('rrr <noc nod!', '<noc nod!', 'wrong closing symbol');

