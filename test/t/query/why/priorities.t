use TestCupt;
use Test::More tests => 12;

use strict;
use warnings;

my $cupt;

eval get_inc_code('common');
eval get_inc_code('setup-from-links');

sub test {
	my ($dependency_graph, $expected_chain_head) = @_;

	my $package = 'xxx';
	$cupt = setup_cupt_from_links($dependency_graph);

	my $options = '-o cupt::resolver::keep-suggests=yes';

	my $graph_comment = join(", ", @$dependency_graph);
	my $comment = "($graph_comment), $package --> $expected_chain_head";
	test_why_regex($package, $options, qr/^$expected_chain_head /, $comment);
}

test(['aa D xxx', 'bb R xxx'] => 'aa');
test(['aa D xxx', 'bb S xxx'] => 'aa');
test(['aa D ppp', 'ppp D xxx', 'bb D xxx'] => 'bb');

test(['aa D ppp', 'ppp D xxx', 'bb R xxx'] => 'aa');
test(['aa R xxx', 'bb D qq1', 'qq1 D qq2', 'qq2 D xxx'] => 'bb');
test(['aa S xxx', 'bb D xxx'] => 'bb');
test(['aa S xxx', 'bb R xxx'] => 'bb');
test(['aa S xxx', 'bb R qqq', 'qqq D xxx'] => 'bb');
test(['aa S xxx', 'bb R qqq', 'qqq R xxx'] => 'bb');
test(['aa D ppp', 'ppp S xxx', 'bb R qqq', 'qqq D xxx'] => 'bb');
test(['aa D ppp', 'ppp S xxx', 'bb R qqq', 'qqq R xxx'] => 'bb');
test(['aa S ppp', 'ppp S xxx', 'bb R qq1', 'qq1 R qq2', 'qq2 R qq3', 'qq3 R qq4', 'qq4 R xxx'] => 'bb');

