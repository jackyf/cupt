use Test::More tests => 6;

require(get_rinclude_path('FSE'));

my $sources = [
	compose_package_record('ss', '1') . "Build-Depends: bd1\n" ,
	compose_package_record('ss', '2') . "Build-Depends: bd2\n" ,
];

my $packages = [
	compose_package_record('a', '1') . "Source: ss\n" ,
	compose_package_record('b', '3') . "Source: ss\n" ,
	compose_package_record('c', '4') . "Source: ss (2)\n" ,
	compose_package_record('d', '1') . "Source: diffs\n" ,
	compose_package_record('bd1', '0') ,
	compose_package_record('bd2', '0') ,
];

my $cupt = setup('packages' => $packages, 'sources' => $sources);

sub bd_bts {
	return 'build-depends(binary-to-source(' . $_[0] . '))';
}

eis($cupt, bd_bts('Pn(a)'), qw(bd1));
eis($cupt, bd_bts('Pn(b)'), ());
eis($cupt, bd_bts('Pn(c)'), qw(bd2));
eis($cupt, bd_bts('Pn(d)'), ());
eis($cupt, bd_bts('Pn(missing)'), ());
eis($cupt, bd_bts('Pn(a)|Pn(c)'), qw(bd1 bd2));

