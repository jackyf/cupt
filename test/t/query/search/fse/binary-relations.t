use TestCupt;
use Test::More tests => 24;

use strict;
use warnings;

my $inst1 = compose_installed_record('aaa', 10) . <<END;
Depends: bbb | ccc
Recommends: ddd
END

my $inst2 = compose_installed_record('bbb', 5) . <<END;
Suggests: eee
END

my $inst3 = compose_installed_record('ccc', 3) . <<END;
Pre-Depends: eee
Enhances: aaa
Conflicts: bbb
END

my $inst4 = compose_installed_record('ddd', 18) . <<END;
Replaces: eee
END

my $inst5 = compose_installed_record('eee', 9) . <<END;
Breaks: ccc
END

my $installed =
		entail($inst1) .
		entail($inst2) .
		entail($inst3) .
		entail($inst4) .
		entail($inst5);

my $cupt = TestCupt::setup('dpkg_status' => $installed);

eval(get_inc_code('FSE'));

my $paaa = pn('aaa');
my $pbbb = pn('bbb');
my $pccc = pn('ccc');
my $peee = pn('eee');
my $pddd = pn('ddd');

eis("pre-depends($pccc)", qw(eee));
eis("depends($paaa)", qw(bbb ccc));
eis("recommends($paaa)", qw(ddd));
eis("suggests($pbbb)", qw(eee));
eis("enhances($pccc)", qw(aaa));
eis("conflicts($pccc)", qw(bbb));
eis("breaks($peee)", qw(ccc));
eis("replaces($pddd)", qw(eee));

eis("conflicts($paaa)", ());
eis("recommends($pccc)", ());


eis("reverse-pre-depends($peee)", qw(ccc));
eis("reverse-depends($pbbb)", qw(aaa));
eis("reverse-depends($pccc)", qw(aaa));
eis("reverse-depends(or($pbbb,$pccc))", qw(aaa));
eis("reverse-recommends($pddd)", qw(aaa));
eis("reverse-suggests($peee)", qw(bbb));
eis("reverse-enhances($paaa)", qw(ccc));
eis("reverse-conflicts($pbbb)", qw(ccc));
eis("reverse-breaks($pccc)", qw(eee));
eis("reverse-replaces($peee)", qw(ddd));

eis("reverse-depends($peee)", ());
eis("reverse-enhances($pbbb)", ());

eis("depends($paaa) & reverse-conflicts($pbbb)", qw(ccc));
eis("recommends(reverse-depends(reverse-suggests($peee)))", qw(ddd));

