use Test::More tests => 17 + 4;

require(get_rinclude_path('FSE'));

my $installed = [
	compose_installed_record('a', '1') ,
	compose_installed_record('b', '2') ,
	compose_installed_record('c', '3') ,
	compose_installed_record('d', '4') ,
];
my $cupt = TestCupt::setup('dpkg_status' => $installed);

my $pa = pn('a');
my $pb = pn('b');
my $pc = pn('c');
my $pnone = pn('none');

eis($cupt, "and($pa)", qw(a));
eis($cupt, "and($pa,$pa)", qw(a));
eis($cupt, "$pa&$pb", ());

eis($cupt, "$pa|$pb", qw(a b));
eis($cupt, "or($pa)", qw(a));
eis($cupt, "or($pa,$pa)", qw(a));

eis($cupt, "not($pa)", qw(b c d));
eis($cupt, "not($pnone)", qw(a b c d));
eis($cupt, "not(package:name(.*))", ());
eis($cupt, "not($pa|$pb)", qw(c d));

eis($cupt, "xor($pa,$pb)", qw(a b));
eis($cupt, "xor($pa,$pa)", ());
eis($cupt, "xor($pa|$pb,$pb|$pc)", qw(a c));

eis($cupt, "or($pa|$pb) & or($pb|$pc)", qw(b));
eis($cupt, "or(or($pa,$pb), or($pb,$pc))", qw(a b c));
eis($cupt, "and(not($pa), not($pb), not($pc))", qw(d));

eis($cupt, "$pa & with(_v, $pa, _v)", qw(a));


eis($cupt, "$pa | $pb | $pc", qw(a b c));
eis($cupt, "$pa & $pb & $pc", qw());
eis($cupt, "$pa & $pb | $pc", qw(c));
eis($cupt, "$pa | $pb & $pc", qw(a));

