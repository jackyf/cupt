use Test::More tests => 2*2+2+4;

my $reinstreq_status_line = 'install reinstreq installed';
my $default_inst_record = compose_installed_record('abc', 1, 'status-line' => $reinstreq_status_line);
my $avail_package = compose_package_record('abc', 1);


sub test_lone_package {
	my $status_line = shift;

	my $installed = compose_installed_record('abc', 1, 'status-line' => $status_line);

	subtest "lone package without candidates ($status_line)" => sub {
		my $cupt = setup('dpkg_status' => [$installed]);

		my $output = get_first_offer("$cupt install");
		like($output, regex_offer(), "there is an offer");
		is(get_offered_version($output, 'abc'), get_empty_version(), "package 'abc' is removed");
	};

	subtest "lone package with candidates ($status_line)" => sub {
		my $cupt = setup('dpkg_status' => [$installed], 'packages' => [$avail_package]);

		my $output = get_first_offer("$cupt install");
		like($output, regex_offer(), "there is an offer");
		is(get_offered_version($output, 'abc'), '1', "package 'abc' is reinstalled");
		like($output, qr/will be reinstalled/, 'showed as to reinstall');
	};
}

test_lone_package($reinstreq_status_line);
test_lone_package('install ok half-installed');


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


my $rdep_inst_record = compose_installed_record('def', 2) . "Depends: abc\n";

subtest "rdeps present, no candidates" => sub {
	my $cupt = setup('dpkg_status' => [ $default_inst_record, $rdep_inst_record ]);
	my $output = get_first_offer("$cupt install");
	is_deeply(get_offered_versions($output), {'abc' => get_empty_version(), 'def' => get_empty_version()});
};

subtest "rdeps present, good candidate" => sub {
	my $cupt = setup(
		'dpkg_status' => [ $default_inst_record, $rdep_inst_record ],
		'packages' => [ $avail_package ],
	);
	my $output = get_first_offer("$cupt install");
	is_deeply(get_offered_versions($output), {'abc' => 1});
};

subtest "rdeps present, broken version stays" => sub {
	my $cupt = setup('dpkg_status' => [ $default_inst_record, $rdep_inst_record ]);
	my $output = get_first_offer("$cupt install abc/improperly-installed");
	is_deeply(get_offered_versions($output), {'def' => get_empty_version()});
};

subtest "rdeps present (virtual package), broken versions stays" => sub {
	my $virt_rdep_inst_record = compose_installed_record('ghi', 2) . "Depends: vr\n";
	my $inst_record = compose_installed_record('abc', 1, 'status-line' => $reinstreq_status_line) . "Provides: vr\n";
	my $cupt = setup('dpkg_status' => [ $inst_record, $virt_rdep_inst_record ]);
	my $output = get_first_offer("$cupt install abc/improperly-installed");
	is_deeply(get_offered_versions($output), {'ghi' => get_empty_version()});
}

