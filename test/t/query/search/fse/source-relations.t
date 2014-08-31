use TestCupt;
use Test::More tests => 4;

use strict;
use warnings;

my $source1 = compose_package_record('sm', '1') . <<END;
Build-Depends: bx
Build-Conflicts-Indep: by
END

my $source2 = compose_package_record('sn', '2') . <<END;
Build-Conflicts: by
Build-Depends-Indep: bx
END

my $sources = entail($source1) . entail($source2);

my $packages =
		entail(compose_package_record('bx', '3')) .
		entail(compose_package_record('by', '4'));

my $cupt = TestCupt::setup('packages' => $packages, 'sources' => $sources);

eval(get_inc_code('FSE'));

my $psm = pn('sm');
my $psn = pn('sn');

eis("build-depends($psm)", qw(bx));
eis("build-conflicts($psn)", qw(by));
eis("build-depends-indep($psn)", qw(bx));
eis("build-conflicts-indep($psm)", qw(by));

