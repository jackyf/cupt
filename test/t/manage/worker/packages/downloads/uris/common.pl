require(get_rinclude_path('../../common'));

sub test_uris {
	my ($comment, $cupt, $subcommand, $uris) = @_;

	my $output = stdall(get_worker_command($cupt, $subcommand));

	my @downloads = ($output =~ m/^S: downloading: (.*)$/mg);

	is_deeply(\@downloads, $uris, $comment)
			or diag($output);
}

sub test_uris_for_bbb_and_ccc {
	my (%p) = @_;

	my $cupt = setup(
		'dpkg_status' => [
			compose_installed_record('aaa', 1) ,
			compose_installed_record('bbb', 2) ,
		],
		'releases' => $p{'releases'},
		'debdelta_conf' => $p{'debdelta_conf'},
		'debpatch' => $p{'debpatch'},
	);

	test_uris($p{'comment'}, $cupt, "install bbb ccc --remove aaa",
			[ $p{'expected_bbb'}, $p{'expected_ccc'} ]);
};

