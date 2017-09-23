use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

require(get_rinclude_path('v2plus'));
set_parameters(2, 5, 2);
do_tests();

