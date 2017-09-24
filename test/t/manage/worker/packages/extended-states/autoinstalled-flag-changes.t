use Test::More tests => 18;

eval get_inc_code('common');

sub check_autoflag {
	my ($new_extended_states, $package, $expected_autoflag_state) = @_;

	my $autoflag_comment = 'autoflag state';
	if ($expected_autoflag_state) {
		like($new_extended_states, qr/^Package: $package\nAuto-Installed: 1\n\n/m, $autoflag_comment);
	} else {
		unlike($new_extended_states, qr/^Package: $package\n/m, $autoflag_comment);
	}
}

sub test {
	my ($command, $package, $expected_autoflag_state) = @_;

	my $cupt = setup(
		'dpkg_status' => [
			compose_installed_record('aa', 1) ,
			compose_installed_record('bb', 1) ,
		],
		'releases' => [{
			'packages' => [
				compose_package_record('bb', 2) ,
				compose_package_record('cc', 2) . "Depends: bb (= 2)\n" ,
			],
			'deb-caches' => 1,
		}],
		'extended_states' => [ compose_autoinstalled_record('bb') ],
	);

	my $output = stdall(get_worker_command($cupt, "$command --no-auto-remove", 'simulate'=>0));
	my $exitcode = $?;
	my $comment = "after command '$command', autoflag for $package is $expected_autoflag_state";

	subtest $comment => sub {
		is($exitcode, 0, 'command succeeded')
				or diag($output);

		my $new_extended_states = get_new_extended_states_content();
		check_autoflag($new_extended_states, $package, $expected_autoflag_state);
	}
}

test('remove aa', 'aa' => 0);
test('remove aa', 'bb' => 1);
test('remove bb', 'bb' => 0);

test('markauto aa', 'aa' => 1);
test('markauto bb', 'aa' => 0);
test('markauto bb', 'bb' => 1);
test('unmarkauto aa', 'aa' => 0);
test('unmarkauto bb', 'bb' => 0);
test('unmarkauto aa', 'bb' => 1);

test('install bb', 'bb' => 1);
test('install --asauto=default bb', 'bb' => 1);
test('install --asauto=yes bb', 'bb' => 1);
test('install --asauto=no bb', 'bb' => 0);

test('install cc', 'cc' => 0);
test('install cc', 'bb' => 1);
test('install cc --unmarkauto bb', 'bb' => 0);
test('install --asauto=yes cc', 'cc' => 1);
test('install cc --markauto cc', 'cc' => 1);

