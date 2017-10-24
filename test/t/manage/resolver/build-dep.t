use Test::More tests => 5+2+3+4+1;

sub test {
	my ($text, $pp_tags, $expected_changes) = @_;

	my $cupt = setup(
		'dpkg_status' => [
			compose_installed_record('yy', 1) ,
		],
		'sources' => [
			compose_package_record('pp', 1) . join("", @$pp_tags) ,
		],
		'packages' => [
			compose_package_record('xx', 1) ,
			compose_package_record('yy', 3) ,
			compose_package_record('nn', 1) ,
			compose_package_record('zz', 1) . "Recommends: xx\n" ,
			compose_package_record('qq', 1) . "Suggests: xx\n" ,
		],
	);

	my $offer = get_first_offer("$cupt build-dep pp");
	my $got_changes = get_offered_versions($offer);
	is_deeply($got_changes, $expected_changes, $text) or diag($offer);
}

sub bd {
	my $arg = shift;
	return "Build-Depends: $arg\n";
}

sub bdi {
	my $arg = shift;
	return "Build-Depends-Indep: $arg\n";
}

sub bc {
	my $arg = shift;
	return "Build-Conflicts: $arg\n";
}

sub bci {
	my $arg = shift;
	return "Build-Conflicts-Indep: $arg\n";
}

my $nv = get_empty_version();

test('no build dependencies', [] => {});
test('new package to install', [bd('xx')] => {'xx' => 1});
test('needed packages already installed', [bd('yy')] => {});
test('needs a higher version', [bd('yy (>= 2)')] => {'yy' => 3});
test('two dependencies', [bd('xx, nn')] => {'xx' => 1, 'nn' => 1});

test('recommends are ignored', [bd('zz')] => {'zz' => 1});
test('suggests are ignored', [bd('qq')] => {'qq' => 1});

test('build-depends-indep', [bdi('xx')] => {'xx' => 1});
test('build-depends-indep plus build-depends',
		[bd('xx'), bdi('nn')] => {'xx' => 1, 'nn' => 1});
test('tag order does not matter', [bdi('xx'), bd('nn')] => {'xx' => 1, 'nn' => 1});

test('conflicts with non-installed', [bc('unkn')] => {});
test('conflicts with installed', [bc('yy')] => {'yy' => $nv});
test('versioned conflicts', [bc('yy (= 1)')] => {'yy' => 3});
test('build-conflicts-indep', [bci('yy')] => {'yy' => $nv});

test('many tags', [bc('unkn'), bci('yy'), bdi('nn')] => {'yy' => $nv, 'nn' => 1});

