use TestCupt;
use Test::More tests => 6;

use strict;
use warnings;

my $source1 = compose_package_record('sm', '10') . <<END;
Binary: bm, bm2
END

my $source2 = compose_package_record('sn', '2');

my $sources = entail($source1) . entail($source2);

my $package1 = compose_package_record('bm', '1');
my $package2 = compose_package_record('bm', '10');
my $package3 = compose_package_record('bm', '100');
my $package4 = compose_package_record('bm', '11') . <<END;
Source: sm (10)
END
my $package5 = compose_package_record('bm2', '10');

my $packages = entail($package1) . entail($package2) .
		entail($package3) . entail($package4) . entail($package5);

my $cupt = TestCupt::setup('packages' => $packages, 'sources' => $sources);

eval(get_inc_code('FSE'));

eis($cupt, "source-to-binary(Pn(sn))", ());
eis($cupt, "source-to-binary(Pn(sm))", qw(bm bm2));
eis($cupt, "source-to-binary(Pn(sm)) & version(1)", ());
eis($cupt, "source-to-binary(Pn(sm)) & version(10)", qw(bm bm2));
eis($cupt, "source-to-binary(Pn(sm)) & version(100)", ());
eis($cupt, "source-to-binary(Pn(sm)) & version(11)", qw(bm));

