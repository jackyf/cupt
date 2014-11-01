my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('dpkg', 0)) .
		entail(compose_installed_record('aaa', 1)) .
		entail(compose_installed_record('bbb', 2)),
	'packages2' => $packages2,
	'debdelta_conf' => $debdelta_conf,
);

my $output = stdall("$cupt -s -y install bbb ccc --remove aaa");

my @downloads = ($output =~ m/^S: downloading: (.*)$/mg);

subtest 'planned downloads' => sub {
	is(scalar @downloads, 2, "2 downloads are planned");
	is($downloads[0], $expected_bbb, 'download of bbb');
	is($downloads[1], $expected_ccc, 'download of ccc');
} or diag($output);

