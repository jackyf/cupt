my $p = 'ppp';
my $q = 'qqq';
my $x = 'xxx';

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		$installed,
	'packages' =>
		entail(compose_package_record($x, 8) . "Provides: vxorp3\n") .
		entail(compose_package_record($p, 5) . "Provides: v5or4\n") .
		entail(compose_package_record($p, 4) . "Provides: v5or4, v6or4\n") .
		entail(compose_package_record($p, 6) . "Provides: v6or4, mixed\n") .
		entail(compose_package_record($p, 3) . "Provides: mixed, vxorp3\n") .
		entail(compose_package_record($q, 7) . "Provides: mixed\n") .
		entail(compose_package_record($q, 5) . "Provides: mixed\n") .
		entail(compose_package_record($q, 3)),
);

sub test {
	my ($command, $expected_versions, $comment) = @_;

	my $output = get_all_offers("$cupt $command -V");
	my @offers = split(regex_offer(), $output);
	pop @offers; # last one will be 'no solutions anymore'

	my @proposed_versions = map { get_offered_version($_, $p) } @offers;

	is_deeply(\@proposed_versions, $expected_versions, $comment) or diag($output);
}

