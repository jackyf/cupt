use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

require(get_rinclude_path('v2plus'));
set_parameters(3, 9, 4);
do_tests();

