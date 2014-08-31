use TestCupt;
use Test::More tests => 9;

use strict;
use warnings;

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

my $sources = entail($source1) . entail($source2);

my $packages =
		entail(compose_package_record('bx', '3')) .
		entail(compose_package_record('by', '4')) .
		entail(compose_package_record('bm', '1')) .
		entail(compose_package_record('bn', '2'));

my $cupt = TestCupt::setup('packages' => $packages, 'sources' => $sources);

eval(get_inc_code('FSE'));

my $psm = pn('sm');
my $psn = pn('sn');

eis("build-depends($psm)", qw(bx));
eis("build-conflicts($psn)", qw(by));
eis("build-depends-indep($psn)", qw(bx));
eis("build-conflicts-indep($psm)", qw(by));

my $pbx = pn('bx');
my $pby = pn('by');

eis("source-to-binary(reverse-build-depends($pbx))", qw(bm));
eis("source-to-binary(reverse-build-conflicts($pby))", qw(bn));
eis("source-to-binary(reverse-build-depends-indep($pbx))", qw(bn));
eis("source-to-binary(reverse-build-conflicts-indep($pby))", qw(bm));

eis("$pbx & build-depends($psm)", qw(bx));

