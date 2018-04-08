use Test::More tests => 2;

require(get_rinclude_path('FSE'));

my $cupt = setup(
	'packages' => [
		compose_package_record('aa', 1) ,
	],
	'sources' => [
		compose_package_record('sa', 0) . "Build-Depends: aa\n" ,
		compose_package_record('sb', 0) . "Build-Depends: bb\n" ,
		compose_package_record('sc', 0) . "Build-Depends: aa (>= 2)\n" ,
		compose_package_record('sd', 0) . "Build-Depends: aa (<< 2)\n" ,
	],
);

eis_source($cupt, 'reverse-build-depends(Pn(aa))', qw(sa sd));
eis_source($cupt, 'ZRbd(Pn(aa))', qw(sa sd));

