use TestCupt;
use Test::More tests => 1;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'packages' =>
		entail(compose_package_record('a', 2) . "Conflicts: b\n") .
		entail(compose_package_record('b', 1)),
);

my $offer = get_first_offer("$cupt install a -o cupt::resolver::max-leaf-count=1");
like($offer, regex_offer());

