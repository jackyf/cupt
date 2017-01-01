use Test::More tests => 5*3;

my $cupt;
eval get_inc_code('common');

sub get_dpkg_sequence {
	my $action = shift;
	return () if not defined $action;
	return (["--$action", [], ['<pp 1>']]) if $action eq 'install';
	return (["--$action", [], ['pp']]);
}

sub test_group {
	my ($state, $action_if_not_available, $action_if_available) = @_;

	$cupt = setup(
		'dpkg_status' => [ compose_status_record('pp', "install ok $state", 1) ],
		'packages' => [ compose_package_record('pp', 1) ],
	);

	test_dpkg_sequence("remove pp # removing a package in state '$state'",
			get_dpkg_sequence('remove'));

	test_dpkg_sequence("install --include-archives=nothing # install/finish a package in state '$state', original package not available",
			get_dpkg_sequence($action_if_not_available));

	test_dpkg_sequence("install # install/finish a package in state '$state'",
			get_dpkg_sequence($action_if_available));
}

test_group('unpacked' => 'configure', 'configure');
test_group('half-installed' => 'remove', 'install');
test_group('half-configured' => 'configure', 'configure');
test_group('triggers-awaited' => undef, undef);
test_group('triggers-pending' => undef, undef);

