use Test::More tests => 9;

my $cupt = setup(
	'dpkg_status' => [
		compose_installed_record('cupt', 0) ,
		compose_installed_record('dpkg', 0) ,
		compose_installed_record('def', 1) ,
		compose_installed_record('pqr', 1) ,
		compose_installed_record('abc', 1) ,
	],
	'packages' => [
		compose_package_record('def', 2) ,
		compose_package_record('pqr', 3) ,
		compose_package_record('xyz', 4) ,
	],
);

sub test {
	my ($text, $arguments, $expected_versions) = @_;

	my $output = get_first_offer("$cupt dist-upgrade $arguments");
	is_deeply(get_offered_versions($output), $expected_versions, $text) or diag($output);
}

test("empty --install", "--install", {'def' => 2, 'pqr' => 3});
test("empty --remove", "--remove", {'def' => 2, 'pqr' => 3});
test("keep one package", "--install def/installed", {'pqr' => 3});
test("keep everything", "--iii '*/installed'", {});
test("remove instead of upgrade", "--remove pqr", {'def' => 2, 'pqr' => get_empty_version()});
test("remove non-upgradeable", "--remove abc", {'def' => 2, 'pqr' => 3, 'abc' => get_empty_version()});
test("also install new package", "--install xyz", {'def' => 2, 'pqr' => 3, 'xyz' => 4});
test("also install new package (short syntax)", "xyz", {'def' => 2, 'pqr' => 3, 'xyz' => 4});
test("multiple changes", "--install xyz --install pqr/installed", {'def' => 2, 'xyz' => 4});

