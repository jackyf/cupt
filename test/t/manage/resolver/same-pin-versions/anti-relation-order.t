use Test::More tests => 6;

require(get_rinclude_path('relation-common'));

init([ compose_installed_record('ppp', 0) ]);

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
test("$ccp '$p (= 3)-'", [ 6, 5, 4, $ev ], 'version 3 disallowed by real relation');
test("$ccp vxorp3-", [ 6, 5, 4, $ev ], 'version 3 disallowed by virtual relation');

