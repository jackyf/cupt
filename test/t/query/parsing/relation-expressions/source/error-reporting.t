use Test::More tests => 3;

my $package = 'qqw';

sub compose_bd_record {
      my ($bd_line) = @_;
      return entail(compose_package_record($package, '0') . "Build-Depends: $bd_line\n");
}

sub test {
	my ($bd, $broken_part, $desc) = @_;

	my $cupt = setup('sources' => compose_bd_record($bd));

	my $output = stdall("$cupt showsrc $package");
	like($output, qr/^E: unable to parse architecture filters \Q'$broken_part'\E/m, $desc);
}

test('sdf [armb] ', '[armb] ', 'space after closing bracket');
test('sdf  [', '[', 'no architectures');
test('sdf [armb, ppq]', '[armb', 'wrong architecture delimiter');

