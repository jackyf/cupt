use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

my $hook_version = 3;
my $package_line_field_count = 9;
my $version_sign_field_index = 4;

eval(get_inc_code('v2plus'));

