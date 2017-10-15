use TestCupt;

my $p = 'ppp';
my $q = 'qqq';
my $x = 'xxx';

my $cupt;

sub init {
	my $installed = shift;

	$cupt = setup(
		'dpkg_status' => $installed,
		'packages' => [
			compose_package_record($x, 8) . "Provides: vxorp3\n" ,
			compose_package_record($p, 5) . "Provides: v5or4\n" ,
			compose_package_record($p, 4) . "Provides: v5or4, v6or4\n" ,
			compose_package_record($p, 6) . "Provides: v6or4, mixed\n" ,
			compose_package_record($p, 3) . "Provides: mixed, vxorp3\n" ,
			compose_package_record($q, 7) . "Provides: mixed\n" ,
			compose_package_record($q, 5) . "Provides: mixed\n" ,
			compose_package_record($q, 3) ,
		]
	);
}

sub test {
	my ($command, $expected_versions, $comment) = @_;

	my $output = get_all_offers("$cupt $command");
	my @offers = split_offers($output);

	my @proposed_versions = map { get_offered_version($_, $p) } @offers;

	is_deeply(\@proposed_versions, $expected_versions, $comment) or diag($output);
}

1;

