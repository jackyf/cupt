use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $packages =
		entail(compose_package_record('abc', '1')) .
		entail(compose_package_record('abc', '2') . "Multi-Arch: allowed\n");

my $cupt = TestCupt::setup('packages' => $packages);

subtest "':any' limits out non-Multi-Arch packages, subtest1" => sub {
	my $output = get_all_offers("$cupt satisfy 'abc:any'");

	is(get_offer_count($output), 1, "only one offer") or
			return diag($output);
	is(get_offered_version($output, 'abc'), '2', "'abc 2' is offered") or
			diag($output);
};

subtest "':any' limits out non-Multi-Arch packages, subtest2" => sub {
	my $output = get_first_offer("$cupt satisfy 'abc:any (<< 2)'");

	like($output, regex_no_solutions(), "no solutions offered");
};

