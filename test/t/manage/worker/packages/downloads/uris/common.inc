eval get_inc_code('../../common');

sub test_uris {
	my (%p) = @_;

	my $cupt = setup_for_worker(
		'dpkg_status' =>
			entail(compose_installed_record('aaa', 1)) .
			entail(compose_installed_record('bbb', 2)),
		'packages2' => $p{'packages2'},
		'debdelta_conf' => $p{'debdelta_conf'},
		'debpatch' => $p{'debpatch'},
	);

	my $output = stdall("$cupt -s -y install bbb ccc --remove aaa");

	my @downloads = ($output =~ m/^S: downloading: (.*)$/mg);

	subtest $p{'comment'} => sub {
		is(scalar @downloads, 2, "2 downloads are planned");
		is($downloads[0], $p{'expected_bbb'}, 'download of bbb');
		is($downloads[1], $p{'expected_ccc'}, 'download of ccc');
	} or diag($output);
};

