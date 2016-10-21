use Test::More tests => 3+2+4;

require(get_rinclude_path('common'));

sub compose_release {
	my @components = map { {'component' => $_, 'packages' => compose_package_record($_, 0)} } @_;
	return {
		'location' => 'remote',
		'components' => \@components,
	};
}

sub check_packages_presence {
	my ($cupt, $packages) = @_;

	my $output = stdall("$cupt show '*'");
	my @got_packages = ($output =~ m/^Package: (.*?)$/mg);
	is_deeply(\@got_packages, $packages, "expected packages: @$packages")
			or diag($output);
}

sub test {
	my ($local_components, $remote_components, $exp_update_result, $exp_components, $comment) = @_;
	subtest $comment => sub {
		my $cupt = setup(
			'releases' => [ compose_release(split(/,/,$local_components)) ],
		);
		update_remote_releases(compose_release(split(/,/,$remote_components)));

		check_exit_code("$cupt update", $exp_update_result, "update result is $exp_update_result");
		check_no_partial_files();
		check_packages_presence($cupt, [split(/,/,$exp_components)]);
	}
}

test('c1', 'c1' => 1, 'c1', 'one component locally and remotely');
test('c1,c2', 'c1,c2' => 1, 'c1,c2', 'two components locally and remotely');
test('ca,cb,cd,cz,ccc', 'ca,cb,cd,cz,ccc' => 1, 'ca,cb,ccc,cd,cz', 'five components locally and remotely');

test('c1', 'c1,c2' => 1, 'c1', 'one component locally, two remotely');
test('c1,c3', 'c1,c2,c3,c4' => 1, 'c1,c3', 'two components locally, four remotely');

test('c1', '' => 0, '', 'no components remotely');
test('c1', 'c2' => 0, '', 'remote component does not match');
test('c1,c2', 'c2,c3' => 0, 'c2', 'partial remote component match');
test('c1,c2,c3,c4', 'c1,c2,c4' => 0, 'c1,c2,c4' => 'all except one components match');

