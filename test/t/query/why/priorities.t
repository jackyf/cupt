use Test::More tests => 22;

require(get_rinclude_path('common'));
require(get_rinclude_path('setup-from-links'));

sub test {
	my ($dependency_graph, $expected_chain_head) = @_;

	my $package = 'xxx';
	my $cupt = setup_cupt_from_links($dependency_graph);

	my $options = '-o cupt::resolver::keep-suggests=yes';

	my $graph_comment = join(", ", @$dependency_graph);
	my $comment = "($graph_comment), $package --> $expected_chain_head";
	test_why_regex($cupt, $package, $options, qr/^$expected_chain_head /, $comment);
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
test(['aa S xxx', 'bb R qqq', 'qqq R yyy', 'yyy R xxx'] => 'aa');
test(['aa D ppp', 'ppp S xxx', 'bb R qqq', 'qqq D xxx'] => 'bb');
test(['aa D ppp', 'ppp S xxx', 'bb R qqq', 'qqq R xxx'] => 'bb');
test(['aa S ppp', 'ppp S xxx', 'bb R qq1', 'qq1 R qq2', 'qq2 R qq3', 'qq3 R qq4', 'qq4 R xxx'] => 'bb');

test(['aa D yyy|xxx', 'bb D xxx'] => 'bb');
test(['aa D yyy|xxx', 'bb R xxx'] => 'aa');
test(['aa D yyy|xxx|zzz', 'bb D zzz|xxx|yyy'] => 'aa');
test(['aa D yyy|zzz|xxx', 'bb D zzz|xxx|yyy'] => 'bb');
test(['aa D yy1|yy2|yy3|xxx', 'bb R xxx'] => 'aa');
test(['aa D yy1|yy2|yy3|yy4|yy5|yy6|yy7|xxx', 'bb R xxx'] => 'bb');
test(['aa D yy1|yy2|yy3|yy4|yy5|yy6|yy7|xxx', 'bb S xxx'] => 'aa');
test(['aa D yyy|zzz|yy1', 'yy1 D yyy|zzz|yy2', 'yy2 D yyy|zzz|xxx', 'bb R xxx'] => 'bb');
test(['aa D zz1', 'aa D zz2|zz3', 'aa D zz3|zz4|zz5|zz6', 'aa D zz7|xxx', 'bb R xxx'] => 'aa');

