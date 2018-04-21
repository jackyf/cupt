use Test::More tests => 3;

my $cupt = setup(
	'packages' => [
		compose_package_record('ba', '1') . "Source: sa\n" ,
	],
	'sources' => [
		compose_package_record('sa', 1) . "Binary: ba\n" ,
	],
);

my $sa = stdout("$cupt showsrc sa");
like($sa, qr/^Package: sa$/m, 'sa is parsed');
is(stdall("$cupt showsrc ba"), $sa, 'ba is converted to sa');
is(stdall("$cupt showsrc 'Pn(ba)'"), $sa, 'via binary FSE');

