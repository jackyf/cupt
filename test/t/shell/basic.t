use Test::More tests => 3+5+2+3;

require(get_rinclude_path('common'));

my $cupt = setup(
	'dpkg_status' => [ compose_installed_record('bb', '2.1-4') ],
	'packages' => [
		compose_package_record('aa', 3) . "Description: aa-desc\n",
		compose_package_record('dd', 4) . "Rtb: xy8pas\n",
	],
	'sources' => [
		compose_package_record('cc', 5) . "Build-Depends: aa\n",
		compose_package_record('ee', 8) . "Wqq: uiop\n",
	],
);
my $cupt_shell = get_shell($cupt);

sub test {
	my ($command, $extra_comment) = @_;
	$extra_comment = defined($extra_comment) ? " ($extra_comment)" : "";

	my $output_normal = stdall("$cupt $command");
	my $output_shell = $cupt_shell->execute($command);
	is($output_shell, $output_normal, "comparing output for '$command'$extra_comment");
	diag($output_shell);
}

sub test_manage {
	my @params = @_;
	$params[0] .= " -s -y -VN";
	test(@params);
}

TODO: {
	local $TODO = 'fix help in shell';
	test('help');
}
test('policy');
test('policysrc');

test('show bb');
test('show aa');
TODO: {
	local $TODO = 'fix displaying extra fields';
	test('showsrc cc');
	test('show dd');
	test('showsrc ee');
}

test('show --installed-only bb');
test('show aa', 'non-installed package after --installed-only');

test_manage('install aa');
test_manage('remove bb');
test_manage('build-dep cc');

