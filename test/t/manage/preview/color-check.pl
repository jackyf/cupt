use Term::ANSIColor;

use strict;
use warnings;

sub check_color_part {
	my ($desc, $output, $str, $attrs) = @_;

	my $color_prefix = defined($attrs) ? color($attrs) : '';
	my $color_suffix = defined($attrs) ? color('reset') : '';
	my $expected_regex = qr/\Q$color_prefix\E$str\Q$color_suffix\E/;
	my $printed_attrs = $attrs//'';

	like($output, $expected_regex, "$desc: '$printed_attrs'");
}

1;

