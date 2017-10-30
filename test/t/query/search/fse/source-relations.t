use Test::More tests => 10+10+1;

require(get_rinclude_path('FSE'));

my $source1 = compose_package_record('sm', '1') . <<END;
Binary: bm
Build-Depends: bx
Build-Conflicts-Indep: by
Build-Conflicts-Arch: bz
END

my $source2 = compose_package_record('sn', '2') . <<END;
Binary: bn
Build-Conflicts: by
Build-Depends-Indep: bx
Build-Depends-Arch: bz
END

my $sources = [ $source1, $source2 ];

my $packages = [
	compose_package_record('bx', '3') ,
	compose_package_record('by', '4') ,
	compose_package_record('bz', '5') ,
	compose_package_record('bm', '1') ,
	compose_package_record('bn', '2')
];

my $cupt = setup('packages' => $packages, 'sources' => $sources);

my $psm = pn('sm');
my $psn = pn('sn');

eis($cupt, "build-depends($psm)", qw(bx));
eis($cupt, "Zbd($psm)", qw(bx));
eis($cupt, "build-conflicts($psn)", qw(by));
eis($cupt, "Zbc($psn)", qw(by));
eis($cupt, "build-depends-indep($psn)", qw(bx));
eis($cupt, "Zbdi($psn)", qw(bx));
eis($cupt, "build-conflicts-indep($psm)", qw(by));
eis($cupt, "Zbci($psm)", qw(by));
eis($cupt, "build-depends-arch($psn)", qw(bz));
eis($cupt, "build-conflicts-arch($psm)", qw(bz));

my $pbx = pn('bx');
my $pby = pn('by');
my $pbz = pn('bz');

eis($cupt, "source-to-binary(reverse-build-depends($pbx))", qw(bm));
eis($cupt, "source-to-binary(ZRbd($pbx))", qw(bm));
eis($cupt, "source-to-binary(reverse-build-conflicts($pby))", qw(bn));
eis($cupt, "source-to-binary(ZRbc($pby))", qw(bn));
eis($cupt, "source-to-binary(reverse-build-depends-indep($pbx))", qw(bn));
eis($cupt, "source-to-binary(ZRbdi($pbx))", qw(bn));
eis($cupt, "source-to-binary(reverse-build-conflicts-indep($pby))", qw(bm));
eis($cupt, "source-to-binary(ZRbci($pby))", qw(bm));
eis($cupt, "source-to-binary(reverse-build-depends-arch($pbz))", qw(bn));
eis($cupt, "source-to-binary(reverse-build-conflicts-arch($pbz))", qw(bm));

eis($cupt, "$pbx & build-depends($psm)", qw(bx));

