use Test::More tests => 2;

my $installed = compose_installed_record('abc', 1, 'status-line' => 'install reinstreq installed');

subtest "reinstreq without candidates" => sub {
	my $cupt = TestCupt::setup('dpkg_status' => [$installed]);

	my $output = get_first_offer("$cupt install");
	like($output, regex_offer(), "there is an offer");
	is(get_offered_version($output, 'abc'), get_empty_version(), "package 'abc' is removed");
};

my $package = compose_package_record('abc', 1);

subtest "reinstreq with candidates" => sub {
	my $cupt = TestCupt::setup('dpkg_status' => [$installed], 'packages' => [$package]);

	my $output = get_first_offer("$cupt install");
	like($output, regex_offer(), "there is an offer");
	is(get_offered_version($output, 'abc'), '1', "package 'abc' is reinstalled");
};

