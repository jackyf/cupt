use Test::More tests => 2+2+5+3+3;

require(get_rinclude_path('common'));

my $cupt = setup(
	'dpkg_status' => [
		compose_installed_record('aa', '0.2016'),
		compose_installed_record('bb', '2.1-4'),
	],
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

	subtest "'$command'$extra_comment" => sub {
		test_output_identical_with_non_shell($cupt, $cupt_shell, $command);
	}
}

sub test_manage {
	my @params = @_;
	$params[0] .= " -s -y -N";
	test(@params);
}

test('help');
test('version');

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
test('show --installed-only aa', '--installed-only after the cache is populated with also non-installed packages');

test_manage('install aa');
test_manage('remove bb');
test_manage('build-dep cc');

