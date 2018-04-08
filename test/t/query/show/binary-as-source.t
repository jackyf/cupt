use Test::More tests => 4;

my $cupt = setup(
	'packages' => [
		compose_package_record('ba', '1') . "Source: sa\n" ,
		compose_package_record('bn', '2') . "Source: sn\n" ,
	],
	'sources' => [
		compose_package_record('sa', 1) . "Binary: ba\n" ,
	],
);

my $sa = stdout("$cupt showsrc sa");
like($sa, qr/^Package: sa$/m, 'sa is parsed');
is(stdall("$cupt showsrc ba"), $sa, 'ba is converted to sa');

my $selected_nothing_regex = qr/E: .* selected nothing\nE: error performing the command 'showsrc'\n/;
like(stdall("$cupt showsrc bn"), $selected_nothing_regex, 'no source package for bn');
like(stdall("$cupt showsrc blabla"), $selected_nothing_regex, 'invalid package');

