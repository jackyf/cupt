use Test::More tests => 15;

my $installed = '';

eval get_inc_code('relation-common');

my $p = 'ppp';
my $q = 'qqq';
my $uv = get_unchanged_version();

# no sorting required
test("satisfy '$p (= 3) | $p (= 4) | $p (= 5)'", [ 3, 4, 5 ], 'full order');
test("satisfy v5or4", [ 5, 4 ], 'virtual package, no sorting required');
test("satisfy '$p (= 3) | v5or4'", [ 3, 5, 4 ], 'no crossing, first relation (real package) has preference');
test("satisfy 'v5or4 | $p (= 6)'", [ 5, 4, 6 ], 'no crossing, first relation (virtual) has preference');
test("satisfy 'v5or4 | v6or4'", [ 5, 4, 6 ], 'partial crossing, first relation (virtual) has preference');
test("satisfy 'v5or4 | $p | $p (= 3)'", [ 5, 4, 6, 3 ], 'double crossing, relation order is preserved');
test("satisfy 'v5or4 | $q (= 7) | v6or4'", [ 5, 4, $uv, 6 ], 'partial crossing, other package in between');

# sorting required
test("satisfy $p", [ 6, 5, 4, 3 ], 'no order');
test("satisfy v6or4", [ 6, 4 ], 'virtual package, sorting is required');
test("satisfy '$p (<< 5) | $p'", [ 4, 3, 6, 5 ], 'crossing, first relation (versioned) has preference');
test("satisfy '$p (= 3) | $p'", [ 3, 6, 5, 4 ], 'partial crossing, first relation (real) has preference');

# multi-package
test("satisfy '$q (= 3) | $p (= 4) | $q (= 5) | $p (= 6) | $q (= 7)'", [ $uv, 4, $uv, 6, $uv ], 'multi-package real relations, version intermix');
test("satisfy mixed", [ 6, 3, $uv, $uv ], 'multi-package virtual relation, packages separately');
test("satisfy '$q | mixed | v6or4 | $p'", [ $uv, $uv, $uv, 6, 3, 4, 5 ], 'a bit of everything');
test("satisfy vxorp3", [ 3, $uv ], 'multi-package virtual relation, package order is alphabetic');

