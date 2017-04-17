use Test::More tests => 6;

require(get_rinclude_path('FSE'));

my $sources = [
	compose_package_record('sm', '10') . "Binary: bm, bm2\n" ,
	compose_package_record('sn', '2') ,
];

my $packages = [
	compose_package_record('bm', '1') ,
	compose_package_record('bm', '10') ,
	compose_package_record('bm', '100') ,
	compose_package_record('bm', '11') . "Source: sm (10)\n" ,
	compose_package_record('bm2', '10') ,
];

my $cupt = setup('packages' => $packages, 'sources' => $sources);

eis($cupt, "source-to-binary(Pn(sn))", ());
eis($cupt, "source-to-binary(Pn(sm))", qw(bm bm2));
eis($cupt, "source-to-binary(Pn(sm)) & version(1)", ());
eis($cupt, "source-to-binary(Pn(sm)) & version(10)", qw(bm bm2));
eis($cupt, "source-to-binary(Pn(sm)) & version(100)", ());
eis($cupt, "source-to-binary(Pn(sm)) & version(11)", qw(bm));

