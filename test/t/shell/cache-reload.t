use Test::More tests => 11;

require(get_rinclude_path('common'));

sub our_setup {
	my ($abc_version) = @_;
	return setup('dpkg_status' => [compose_installed_record('abc', $abc_version)]);
}

sub test {
	my ($first_command, $second_command, $expected_reload) = @_;

	my $cupt = our_setup(2);
	my $cupt_shell = get_shell($cupt);

	$cupt_shell->execute($first_command);
	$cupt = our_setup(4);
	if (defined $second_command) {
		$cupt_shell->execute($second_command);
	}

	my $expected_abc_version = $expected_reload ? 4 : 2;
	my $abc_result = $cupt_shell->execute('show abc');
	my $comment = "after '$first_command'" .
			(defined($second_command) ? " and '$second_command'" : '') .
			" cache is " . ($expected_reload?'':'not ') . 'reloaded';

	like($abc_result, qr/^Version: $expected_abc_version/m, to_one_line($comment));
}

test("show abc", undef, 0);
test("policy", undef, 0);

test("remove -y abc", undef, 1);
test("remove abc\nn", undef, 0);

test("show abc", "show xyz", 0);
test("show abc", "showsrc xyz", 1);
test("showsrc xyz", undef, 0);

test("badcmd", undef, 1);
test("show --installed-only xyz", undef, 1);
test("showauto qwe", undef, 1);

test("update", undef, 1);

