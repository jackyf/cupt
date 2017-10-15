use TestCupt;
use Test::More tests => 26;

my $cupt;
my $pin_of_zzz;

sub lsetup {
	$cupt = setup(
		'packages' =>
			entail(compose_package_record('mmm', '0') . "Depends: bb1 | bb2\n") .
			entail(compose_package_record('nnn', '0') . "Depends: bb2 | bb3\n") .
			entail(compose_package_record('bb1', '1') . "Recommends: zzz\n") .
			entail(compose_package_record('bb2', '2') . "Depends: ccc\n") .
			entail(compose_package_record('bb3', '3') . "Recommends: zzz\n") .
			entail(compose_package_record('ccc', '100') . "Recommends: zzz\n") .
			entail(compose_package_record('zzz', '8')),
		'preferences' =>
			compose_version_pin_record('bb1', '*', 700) .
			compose_version_pin_record('bb2', '*', 600) .
			compose_version_pin_record('bb3', '*', 500) .
			compose_version_pin_record('zzz', '*', $pin_of_zzz),
	);
}

sub get_first_offer_for {
	my ($package) = @_;
	return get_first_offer("$cupt install $package -o debug::resolver=yes");
}

sub test {
	my ($package, $bb_version) = @_;

	subtest "$package, pin of zzz: $pin_of_zzz" => sub {
		my $output = get_first_offer_for($package);
		for my $version (1..3) {
			my $bb_package = "bb$version";
			my $expected_version = ($bb_version eq $version) ? $version : get_unchanged_version();
			is(get_offered_version($output, $bb_package), $expected_version) or diag($output);
		}
	};
}

foreach (-5000, -2000, -1000, -500, -200, 0, 100, 300, 500, 700, 900, 1200, 2000) {
	$pin_of_zzz = $_;
	lsetup();

	test('mmm', '1');
	test('nnn', '2');
}

