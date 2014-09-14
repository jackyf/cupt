use TestCupt;
use Test::More tests => 13;

use strict;
use warnings;

sub generate_package_chain_removing_i {
	my ($count) = @_;

	my $result;

	foreach my $index (1..$count-1) {
		my $current_package = "p$index";
		my $next_package = 'p' . ($index+1);
		$result .= entail(compose_package_record($current_package, '7') . "Depends: $next_package\n");
	}
	$result .= entail(compose_package_record("p$count", '8') . "Conflicts: i\n");

	return $result;
}

sub test {
	my ($count) = @_;

	my $cupt = TestCupt::setup(
		'dpkg_status' =>
			entail(compose_installed_record('i', '1')),
		'packages2' =>
			[
				[
					'archive' => 'kk',
					'content' =>
						entail(compose_package_record('a', '6') . "Depends: p1\n") .
						generate_package_chain_removing_i(@_),
				],
				[
					'archive' => 'qq',
					'content' =>
						entail(compose_package_record('a', '4')),
				],
			],
	);

	my $offer = get_first_offer("$cupt install -t kk --sf a -V");
	is(get_offered_version($offer, 'a'), 4, "a 6 depends on the bad chain of $count packages --> a 4 is offered");
}

foreach (1..13) {
	local $TODO = 'decrease starting quality adjustment' if $_ >= 9;
	test(2**$_);
}

