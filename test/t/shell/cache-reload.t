use Test::More tests => 12;

require(get_rinclude_path('common'));

my $cache_version;
sub our_setup {
	my $abc_version = $cache_version++;
	return setup('dpkg_status' => [compose_installed_record('abc', $abc_version)]);
}

sub test {
	my ($commands, $expected_cache_version) = @_;
	$cache_version = 0;

	my $cupt = our_setup();
	my $cupt_shell = get_shell($cupt);

	foreach my $command (@$commands) {
		$cupt_shell->execute($command);
		$cupt = our_setup();
	}

	my $abc_result = $cupt_shell->execute('show abc');
	my $comment = join(' & ', map { to_one_line($_) } @$commands) .
			": loaded cache version is $expected_cache_version";

	like($abc_result, qr/^Version: $expected_cache_version/m, to_one_line($comment));
}

test(['show abc'] => 0);
test(['policy'] => 0);

test(['remove -y abc'] => 1);
test(['remove abc\nn'] => 0);

test(['show abc', 'show xyz'] => 0);
test(['show abc', 'showsrc xyz'] => 1);
test(['showsrc xyz'] => 0);

test(['badcmd'] => 1);
test(['show --installed-only xyz'] => 1);
test(['showauto qwe'] => 1);

test(['update'] => 1);

test(['showsrc xyz', 'show abc', 'showsrc xyz'] => 0);

