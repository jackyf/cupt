use TestCupt;
use Test::More tests => 1;

use strict;
use warnings;

my $hook_version = 2;
my $package_line_field_count = 5;
my $version_sign_field_index = 2;

eval(get_inc_code('v2plus'));

