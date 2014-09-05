use TestCupt;
use Test::More tests => 2;

my $preferences = <<END;
Package: bb1
Pin: version *
Pin-Priority: 700

Package: bb2
Pin: version *
Pin-Priority: 600

Package: bb3
Pin: version *
Pin-Priority: 500
END

my $cupt = TestCupt::setup(
	'packages' =>
		entail(compose_package_record('mmm', '0') . "Depends: bb1 | bb2\n") .
		entail(compose_package_record('nnn', '0') . "Depends: bb2 | bb3\n") .
		entail(compose_package_record('bb1', '1') . "Recommends: zzz\n") .
		entail(compose_package_record('bb2', '2') . "Depends: ccc\n") .
		entail(compose_package_record('bb3', '3') . "Recommends: zzz\n") .
		entail(compose_package_record('ccc', '100') . "Recommends: zzz\n"),
	'preferences' => $preferences,
);

sub get_first_offer_for {
	my ($package) = @_;
	return get_first_offer("$cupt install $package -V");
}

sub test {
	my ($package, $bb_version) = @_;

	subtest $package => sub {
		my $output = get_first_offer_for($package);
		for my $version (1..3) {
			my $bb_package = "bb$version";
			my $expected_version = ($bb_version eq $version) ? $version : get_unchanged_version();
			is(get_offered_version($output, $bb_package), $expected_version) or diag($output);
		}
	};
}

TODO: {
	local $TODO = 'improve score algorithm';
	test('mmm', '1');
}
test('nnn', '2');
