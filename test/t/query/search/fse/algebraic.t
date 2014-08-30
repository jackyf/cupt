use TestCupt;
use Test::More tests => 16;

use strict;
use warnings;

my $installed =
		entail(compose_installed_record('a', '1')) .
		entail(compose_installed_record('b', '2')) .
		entail(compose_installed_record('c', '3')) .
		entail(compose_installed_record('d', '4'));

my $cupt = TestCupt::setup('dpkg_status' => $installed);

eval(get_inc_code('FSE'));

my $pa = pn('a');
my $pb = pn('b');
my $pc = pn('c');
my $pnone = pn('none');

eis("and($pa)", qw(a));
eis("and($pa,$pa)", qw(a));
eis("$pa&$pb", ());

eis("$pa|$pb", qw(a b));
eis("or($pa)", qw(a));
eis("or($pa,$pa)", qw(a));

eis("not($pa)", qw(b c d));
eis("not($pnone)", qw(a b c d));
eis("not(package:name(.*))", ());
eis("not($pa|$pb)", qw(c d));

eis("xor($pa,$pb)", qw(a b));
eis("xor($pa,$pa)", ());
eis("xor($pa|$pb,$pb|$pc)", qw(a c));

eis("or($pa|$pb) & or($pb|$pc)", qw(b));
eis("or(or($pa,$pb), or($pb,$pc))", qw(a b c));
eis("and(not($pa), not($pb), not($pc))", qw(d));

