use Test::More tests => 16;

my $package = 'pkg';

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

test('aa <nocheck>', 'aa', '1 positive');
test('aa <!nocheck>', 'aa', '1 negative');
test('aa (>= 2.3) <stage1>', 'aa (>= 2.3)', 'positive + relation');
test('aa (<< 2:5.2~pre4) <!stage2>', 'aa (<< 2:5.2~pre4)', 'negative + relation');
test('aa <stage2 stage3>', 'aa', '2 positive');
test('aa <stage2  stage3   >', 'aa', '2 positive, whitespaces');
test('aa< stage2 stage3 >  ', 'aa', '2 positive, more whitespaces');
test('aa <!nocheck !stage1 !stage2>', 'aa', '3 negative');
test('aa (>= 2.3) | bb <stage1>', 'aa (>= 2.3) | bb', 'positive + complex relation');
test('aa (>= 2.3) | bb (= 12) <!stage1>', 'aa (>= 2.3) | bb (= 12)', 'negative + complex relation');
test('aa [z1] <cross>', 'aa [z1]', 'positive + architecture');
test('aa [z1 z2] <!cross>', 'aa [z1 z2]', 'negative, 2 architectures');
test('aa (= 256) [!z1] <cross>', 'aa (= 256)', 'position + relation + negative architecture');
test('aa <cross> <stage1>', 'aa', '2 lists');
test('aa <cross> <stage1> <!stage2> <stagex>', 'aa', '4 lists');
test('aa | bb (<< 7:5.4-3) [z1 z2] <cross> <stage1 stage2> <!cross !stage1 !nocheck>',
		'aa | bb (<< 7:5.4-3) [z1 z2]', 'ultimate mix');

