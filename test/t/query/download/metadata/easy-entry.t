use Test::More tests => 3 + 3;

require(get_rinclude_path('common'));

sub init_cupt {
	my ($archive, $component, $line_hook) = @_;
	return setup('releases' => [{
			'archive' => $archive,
			'components' => [{
				'component' => $component,
				'packages' => [ compose_package_record('p3', 2) ],
			}],
			'line-hook' => $line_hook,
			'location' => 'remote',
		}
	]);
}

sub get_desc {
	my ($archive, $component, undef) = @_;
	return "'$archive', '$component'";
}

sub test_success {
	my @init_params = @_;
	my $desc = get_desc(@init_params);
	subtest "$desc => successful fetch" => sub {
		my $cupt = init_cupt(@init_params);
		check_exit_code("$cupt update", 1, 'update succeeded');
		check_exit_code("$cupt show p3", 1, 'package existance (good)');
		check_exit_code("$cupt show p5", 0, 'package existance (bad)');
	};
}

sub test_failure {
	my @init_params = @_;
	my $desc = get_desc(@init_params);
	subtest "$desc => not present" => sub {
		my $cupt = init_cupt(@init_params);
		check_exit_code("$cupt update", 0, 'update failed');
		check_exit_code("$cupt show p3", 0, 'package existance');
	};
}

sub set_arc_and_comp_hook {
	my $repl = shift;
	return sub {
		my $line = shift;
		$line =~ s/arc9.*/$repl/;
		return $line;
	};
};

test_success('arc9', 'qwe', undef);
test_failure('arc9', 'qwe', set_arc_and_comp_hook('arc9'));
test_failure('arc9', 'qwe', set_arc_and_comp_hook('arc9/'));

test_success('arc9/', '', undef);
test_failure('arc9/', '', set_arc_and_comp_hook('arc9'));
test_failure('arc9/', '', set_arc_and_comp_hook('arc9 qwe'));

