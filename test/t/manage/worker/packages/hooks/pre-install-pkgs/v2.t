use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

eval(get_inc_code('v2plus'));
set_parameters(2, 5, 2);
do_tests();

