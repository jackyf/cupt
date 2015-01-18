use TestCupt;
use Test::More tests => 12;

use strict;
use warnings;

my $cupt;

eval get_inc_code('common');

my %dep_letters = (
	'D' => 'Depends',
	'R' => 'Recommends',
	'S' => 'Suggests',
);

sub setup_cupt {
	my $links = shift;

	my %sorted_deps;
	my %autoflags;

	foreach my $link (@$links) {
		my ($from, $type, $to) = split(' ', $link);

		if (length($to) == 3) {
			$autoflags{$to} = 1;
		}

		$sorted_deps{$to} //= {};
		push @{$sorted_deps{$from}->{$type}}, $to;
	}

	my $installed_packages = '';
	while (my ($package, $deplines) = each %sorted_deps) {
		$installed_packages .= compose_installed_record($package, 0);

		while (my ($dep_letter, $dependees) = each %$deplines) {
			$installed_packages .= $dep_letters{$dep_letter};
			$installed_packages .= ': ';
			$installed_packages .= join(', ', @$dependees);
			$installed_packages .= "\n";
		}

		$installed_packages .= "\n";
	}

	my $extended_states = join('', map { entail(compose_autoinstalled_record($_)) } keys %autoflags);

	$cupt = TestCupt::setup('dpkg_status' => $installed_packages, 'extended_states' => $extended_states);
}

sub test {
	my ($dependency_graph, $expected_chain_head) = @_;

	my $package = 'xxx';
	$cupt = setup_cupt($dependency_graph);

	my $options = '-o cupt::resolver::keep-suggests=yes';

	my $graph_comment = join(", ", @$dependency_graph);
	my $comment = "($graph_comment), $package --> $expected_chain_head";
	test_why_regex($package, $options, qr/^$expected_chain_head /, $comment);
}

test(['aa D xxx', 'bb R xxx'] => 'aa');
test(['aa D xxx', 'bb S xxx'] => 'aa');
test(['aa D ppp', 'ppp D xxx', 'bb D xxx'] => 'bb');

TODO: {
	local $TODO = 'not implemented';
	test(['aa D ppp', 'ppp D xxx', 'bb R xxx'] => 'aa');
	test(['aa R xxx', 'bb D qq1', 'qq1 D qq2', 'qq2 D xxx'] => 'bb');
	test(['aa S xxx', 'bb D xxx'] => 'bb');
	test(['aa S xxx', 'bb R xxx'] => 'bb');
	test(['aa S xxx', 'bb R qqq', 'qqq D xxx'] => 'bb');
	test(['aa S xxx', 'bb R qqq', 'qqq R xxx'] => 'bb');
	test(['aa D ppp', 'ppp S xxx', 'bb R qqq', 'qqq D xxx'] => 'bb');
	test(['aa D ppp', 'ppp S xxx', 'bb R qqq', 'qqq R xxx'] => 'bb');
	test(['aa S ppp', 'ppp S xxx', 'bb R qq1', 'qq1 R qq2', 'qq2 R qq3', 'qq3 R qq4', 'qq4 R xxx'] => 'bb');
}

