use Test::More tests => 16 + 2 + 18 + 2 + 2;

require(get_rinclude_path('FSE'));

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

my $installed = [ $inst1, $inst2, $inst3, $inst4, $inst5 ];
my $cupt = setup('dpkg_status' => $installed);

my $paaa = pn('aaa');
my $pbbb = pn('bbb');
my $pccc = pn('ccc');
my $peee = pn('eee');
my $pddd = pn('ddd');

eis($cupt, "pre-depends($pccc)", qw(eee));
eis($cupt, "Ypd($pccc)", qw(eee));
eis($cupt, "depends($paaa)", qw(bbb ccc));
eis($cupt, "Yd($paaa)", qw(bbb ccc));
eis($cupt, "recommends($paaa)", qw(ddd));
eis($cupt, "Yr($paaa)", qw(ddd));
eis($cupt, "suggests($pbbb)", qw(eee));
eis($cupt, "Ys($pbbb)", qw(eee));
eis($cupt, "enhances($pccc)", qw(aaa));
eis($cupt, "Ye($pccc)", qw(aaa));
eis($cupt, "conflicts($pccc)", qw(bbb));
eis($cupt, "Yc($pccc)", qw(bbb));
eis($cupt, "breaks($peee)", qw(ccc));
eis($cupt, "Yb($peee)", qw(ccc));
eis($cupt, "replaces($pddd)", qw(eee));
eis($cupt, "Yrp($pddd)", qw(eee));

eis($cupt, "conflicts($paaa)", ());
eis($cupt, "recommends($pccc)", ());


eis($cupt, "reverse-pre-depends($peee)", qw(ccc));
eis($cupt, "YRpd($peee)", qw(ccc));
eis($cupt, "reverse-depends($pbbb)", qw(aaa));
eis($cupt, "YRd($pbbb)", qw(aaa));
eis($cupt, "reverse-depends($pccc)", qw(aaa));
eis($cupt, "reverse-depends(or($pbbb,$pccc))", qw(aaa));
eis($cupt, "reverse-recommends($pddd)", qw(aaa));
eis($cupt, "YRr($pddd)", qw(aaa));
eis($cupt, "reverse-suggests($peee)", qw(bbb));
eis($cupt, "YRs($peee)", qw(bbb));
eis($cupt, "reverse-enhances($paaa)", qw(ccc));
eis($cupt, "YRe($paaa)", qw(ccc));
eis($cupt, "reverse-conflicts($pbbb)", qw(ccc));
eis($cupt, "YRc($pbbb)", qw(ccc));
eis($cupt, "reverse-breaks($pccc)", qw(eee));
eis($cupt, "YRb($pccc)", qw(eee));
eis($cupt, "reverse-replaces($peee)", qw(ddd));
eis($cupt, "YRrp($peee)", qw(ddd));

eis($cupt, "reverse-depends($peee)", ());
eis($cupt, "reverse-enhances($pbbb)", ());

eis($cupt, "depends($paaa) & reverse-conflicts($pbbb)", qw(ccc));
eis($cupt, "recommends(reverse-depends(reverse-suggests($peee)))", qw(ddd));

