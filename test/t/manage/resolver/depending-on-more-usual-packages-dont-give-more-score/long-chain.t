use TestCupt;
use Test::More tests => 34;

use strict;
use warnings;

sub generate_package_chain_removing_i {
	my ($count) = @_;

	my $result;

	foreach my $index (1..$count-1) {
		my $current_package = "p$index";
		my $next_package = 'p' . ($index+1);
		$result .= entail(compose_package_record($current_package, '7') . "Depends: $next_package (>= 7)\n");
	}
	$result .= entail(compose_package_record("p$count", '8') . "Conflicts: i\n");

	return $result;
}

my $lot_of_installed_packages = join('', map { entail(compose_installed_record("p$_", 5)) } (1..32));

sub test {
	my ($packages_were_installed, $count) = @_;

	my $cupt = TestCupt::setup(
		'dpkg_status' =>
			entail(compose_installed_record('i', '1')) .
			($packages_were_installed ? $lot_of_installed_packages : ''),
		'packages2' =>
			[
				{
					'archive' => 'kk',
					'content' =>
						entail(compose_package_record('a', '6') . "Depends: p1 (>= 7)\n") .
						generate_package_chain_removing_i($count),
				},
				{
					'archive' => 'qq',
					'content' =>
						entail(compose_package_record('a', '4')),
				},
			],
	);

	my $offer = get_first_offer("$cupt install -t kk --sf a -V");
	my $chain_type = ($packages_were_installed ? 'upgraded' : 'new');
	is(get_offered_version($offer, 'a'), 4, "a 6 depends on the bad chain of $count $chain_type packages --> a 4 is offered");
}

# new packages
foreach (0..13) {
	test(0, 2**$_);
}

# upgraded packages
foreach (1..20) {
	local $TODO = 'score of non-explicitly-requested-by-user upgrades could be smaller' if ($_ >= 8);
	test(1, $_);
}

