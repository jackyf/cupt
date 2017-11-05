use Test::More tests => 2*2+2;

sub test_lone_package {
	my $status_line = shift;

	my $installed = compose_installed_record('abc', 1, 'status-line' => $status_line);

	subtest "auto-reinstall, lone package without candidates ($status_line)" => sub {
		my $cupt = setup('dpkg_status' => [$installed]);

		my $output = get_first_offer("$cupt install");
		like($output, regex_offer(), "there is an offer");
		is(get_offered_version($output, 'abc'), get_empty_version(), "package 'abc' is removed");
	};

	subtest "auto-reinstall lone package with candidates ($status_line)" => sub {
		my $package = compose_package_record('abc', 1);
		my $cupt = setup('dpkg_status' => [$installed], 'packages' => [$package]);

		my $output = get_first_offer("$cupt install");
		like($output, regex_offer(), "there is an offer");
		is(get_offered_version($output, 'abc'), '1', "package 'abc' is reinstalled");
		like($output, qr/will be reinstalled/, 'showed as to reinstall');
	};
}

test_lone_package('install reinstreq installed');
test_lone_package('install ok half-installed');

my $default_inst_record = compose_installed_record('abc', 1, 'status-line' => 'install reinstreq installed');

subtest "broken package is forced to stay" => sub {
	my $cupt = setup('dpkg_status' => [$default_inst_record]);
	my $output = get_first_offer("$cupt install abc/improperly-installed");
	like($output, qr/nothing to do/i);
};

subtest "user wish is too weak to prevent auto-reinstallation" => sub {
	my $cupt = setup('dpkg_status' => [$default_inst_record]);
	my $output = get_first_offer("$cupt install --wish abc/improperly-installed");
	is(get_offered_version($output, 'abc'), get_empty_version());
};

