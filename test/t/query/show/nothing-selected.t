use Test::More tests => 5;

my $cupt = setup(
	'packages' => [
		compose_package_record('bn', '2'),
	],
);


sub sn {
	my $cmd = shift;
	return qr/E: .* selected nothing\nE: error performing the command '$cmd'\n/;
}

like(stdall("$cupt show by"), sn('show'), 'invalid package');
like(stdall("$cupt show 'depends(Pn(bn))'"), sn('show'), 'empty FSE result');

like(stdall("$cupt showsrc blabla"), sn('showsrc'), 'invalid package');
like(stdall("$cupt showsrc 'priority(extra)'"), sn('showsrc'), 'empty FSE result');
like(stdall("$cupt showsrc bn"), sn('showsrc'), 'no source package for bn');

