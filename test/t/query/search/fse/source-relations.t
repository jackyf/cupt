use Test::More tests => 9;

require(get_rinclude_path('FSE'));

my $source1 = compose_package_record('sm', '1') . <<END;
Binary: bm
Build-Depends: bx
Build-Conflicts-Indep: by
END

my $source2 = compose_package_record('sn', '2') . <<END;
Binary: bn
Build-Conflicts: by
Build-Depends-Indep: bx
END

my $sources = [ $source1, $source2 ];

my $packages = [
	compose_package_record('bx', '3') ,
	compose_package_record('by', '4') ,
	compose_package_record('bm', '1') ,
	compose_package_record('bn', '2')
];

my $cupt = setup('packages' => $packages, 'sources' => $sources);

my $psm = pn('sm');
my $psn = pn('sn');

eis($cupt, "build-depends($psm)", qw(bx));
eis($cupt, "build-conflicts($psn)", qw(by));
eis($cupt, "build-depends-indep($psn)", qw(bx));
eis($cupt, "build-conflicts-indep($psm)", qw(by));

my $pbx = pn('bx');
my $pby = pn('by');

eis($cupt, "source-to-binary(reverse-build-depends($pbx))", qw(bm));
eis($cupt, "source-to-binary(reverse-build-conflicts($pby))", qw(bn));
eis($cupt, "source-to-binary(reverse-build-depends-indep($pbx))", qw(bn));
eis($cupt, "source-to-binary(reverse-build-conflicts-indep($pby))", qw(bm));

eis($cupt, "$pbx & build-depends($psm)", qw(bx));

