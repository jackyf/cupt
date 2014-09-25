use TestCupt;
use Test::More tests => 6;

use strict;
use warnings;

my $installed = entail(compose_installed_record('ppp', 0));

eval get_inc_code('relation-common');

my $p = 'ppp';
my $ev = get_empty_version();

# "common command part"
my $ccp = "satisfy '$p (= 0)-'";

# no sorting required
test("$ccp $p-", [ $ev ], 'strict removal');
test("$ccp '$p (= 6)-'", [ 5, 4, 3, $ev ], 'version 6 disallowed by real relation');
test("$ccp v5or4-", [ 6, 3, $ev ], 'some versions disallowed by virtual relation');
test("$ccp v5or4- v6or4-", [ 3, $ev ], 'some version disallowed, crossed virtual relations');
# sorting required
TODO: {
	local $TODO = 'sorting not implemented';
	test("$ccp '$p (= 3)-'", [ 6, 5, 4, $ev ], 'version 3 disallowed by real relation');
	test("$ccp vxorp3-", [ 6, 5, 4, $ev ], 'version 3 disallowed by virtual relation');
}

