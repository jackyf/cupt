use Test::More tests => 4;

sub test_lone_package {
	my $status_line = shift;

	my $installed = compose_installed_record('abc', 1, 'status-line' => $status_line);

	subtest "auto-reinstall, lone package without candidates ($status_line)" => sub {
		my $cupt = TestCupt::setup('dpkg_status' => [$installed]);

		my $output = get_first_offer("$cupt install");
		like($output, regex_offer(), "there is an offer");
		is(get_offered_version($output, 'abc'), get_empty_version(), "package 'abc' is removed");
	};


	subtest "auto-reinstall lone package with candidates ($status_line)" => sub {
		my $package = compose_package_record('abc', 1);
		my $cupt = TestCupt::setup('dpkg_status' => [$installed], 'packages' => [$package]);

		my $output = get_first_offer("$cupt install");
		like($output, regex_offer(), "there is an offer");
		is(get_offered_version($output, 'abc'), '1', "package 'abc' is reinstalled");
	};
}

test_lone_package('install reinstreq installed');
test_lone_package('install ok half-installed');

