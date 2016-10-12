use Test::More tests => 1;

require(get_rinclude_path('common'));

sub compose_release_dict {
	my ($packages, $sources) = @_;
	return {
		'packages' => $packages,
		'sources' => $sources,
		'location' => 'remote',
	};
}

my $cupt = setup(
	'releases' => [
		compose_release_dict([
			compose_package_record('abc', 1),
			compose_package_record('def', 2)
		], [
			compose_package_record('rtg', 3),
			compose_package_record('bnm', 4)
		])
	]
);

subtest "simple update" => sub {
	check_exit_code("$cupt show abc", 0, 'no packages available at this point');

	check_exit_code("$cupt update", 1, 'metadata update succeeded');

	check_exit_code("$cupt show abc", 1, 'first binary package');
	check_exit_code("$cupt show def", 1, 'second binary package');
	check_exit_code("$cupt showsrc rtg", 1, 'first source package');
	check_exit_code("$cupt showsrc bnm", 1, 'second binary package');
}

