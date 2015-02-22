sub test_line {
	my ($input, $package, $original_version, $supposed_version, $version_sign, $last_field) = @_;

	subtest "package info line for '$package'" => sub {
		my ($line) = ($input =~ m/^($package .* \Q$last_field\E)$/m);

		isnt($line, undef, 'line found') or return;

		my @fields = split(m/ /, $line);
		is(scalar @fields, $package_line_field_count, 'field count') or return;

		is($fields[1], $original_version, 'original version');
		is($fields[$version_sign_field_index], $version_sign, 'version sign');
		is($fields[$version_sign_field_index+1], $supposed_version, 'supposed version');
	}
}

sub test_configure_line {
	test_line(@_, '**CONFIGURE**');
}

sub test_remove_line {
	test_line(@_, '**REMOVE**');
}

my $hook_options = "-o dpkg::pre-install-pkgs::=vhook -o dpkg::tools::options::vhook::version=$hook_version";
my $confirmation = "y\nYes, do as I say!";
my $cupt;

sub test {
	my ($subcommand, $line_tests) = @_;

	my $offer = stdall("echo '$confirmation' | $cupt -s $subcommand $hook_options");

	subtest "the hook is run with proper input (subcommand: $subcommand)" => sub {
		my ($input) = ($offer =~ m/running command 'vhook' with the input.-8<-\n(.*)->8-/s);

		isnt($input, undef, 'hook is run') or return;

		$input =~ m/^(.*)\n(.*)\n/;
		is($1, "VERSION $hook_version", 'first input line is hook version');
		like($2, qr/^APT::Architecture=/, 'second input line is original-cased apt architecture');

		$line_tests->($input);
	} or diag($offer);
}

eval get_inc_code('../../common');

$cupt = setup_for_worker(
	'dpkg_status' =>
		entail(compose_installed_record('aaa', '2.3')) .
		entail(compose_installed_record('bbb', '0.99-1')) .
		entail(compose_installed_record('ccc', '1.8.1-5')) ,
	'packages' =>
		entail(compose_package_record('bbb', '0.97-4')) .
		entail(compose_package_record('ccc', '1.8.1-6') . "Depends: ddd, eee\n") .
		entail(compose_package_record('ddd', '4:4.11.0-3')) .
		entail(compose_package_record('eee', '22') . "Depends: brk\n") .
		entail(compose_package_record('eee', '22', 'sha' => 'dffe') . "Suggests: brk\n") ,
);
test("full-upgrade --remove aaa --satisfy 'bbb (<< 0.98)'",
		sub {
			my $input = shift;
			test_remove_line($input, 'aaa', '2.3', '-', '>');
			test_configure_line($input, 'bbb', '0.99-1', '0.97-4', '>');
			test_configure_line($input, 'ccc', '1.8.1-5', '1.8.1-6', '<');
			test_configure_line($input, 'ddd', '-', '4:4.11.0-3', '<');
			test_configure_line($input, 'eee', '-', '22', '<');
		});

$cupt = setup_for_worker(
	'dpkg_status' =>
		entail(compose_removed_record('fff'))
);
test("purge fff",
		sub {
			test_remove_line(shift, 'fff', '-', '-', '<');
		});

