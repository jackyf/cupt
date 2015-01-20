use TestCupt;
use Test::More tests => 9;

use strict;
use warnings;

my $cupt;

eval get_inc_code('common');
eval get_inc_code('setup-from-links');

sub build_up_setup_data {
	my ($package, $level) = @_;

	return () if ($level == 0);

	my $unfold = sub {
		my $branch = $_;
		my $child = "$package$branch";

		my $link = "$package D $child";
		my @child_tree = build_up_setup_data($child, $level-1);

		return ($link, @child_tree);
	};

	return map(&$unfold, qw(0 1 2));
}

sub setup_cupt {
	my $level = shift;

	my @links = build_up_setup_data('zz', $level);
	setup_cupt_from_links(\@links);
}

sub test {
	my $level = shift;
	$cupt = setup_cupt($level);

	my $last_package = 'zz' . ('1' x $level);
	test_why_regex($last_package, '', qr/^zz /, "level $level");
}

foreach (1..9) {
	test($_);
}

