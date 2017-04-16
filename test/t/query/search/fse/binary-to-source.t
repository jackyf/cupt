use TestCupt;
use Test::More tests => 6;

use strict;
use warnings;

my $source1 = compose_package_record('ss', '1') . <<END;
Build-Depends: bd1
END

my $source2 = compose_package_record('ss', '2') . <<END;
Build-Depends: bd2
END

my $sources = entail($source1) . entail($source2);

my $package1 = compose_package_record('a', '1') . <<END;
Source: ss
END

my $package2 = compose_package_record('b', '3') . <<END;
Source: ss
END

my $package3 = compose_package_record('c', '4') . <<END;
Source: ss (2)
END

my $package4 = compose_package_record('d', '1') . <<END;
Source: diffs
END

my $packages =
		entail($package1) . entail($package2) . entail($package3) .
		entail($package4) .
		entail(compose_package_record('bd1', '0')) .
		entail(compose_package_record('bd2', '0'));

my $cupt = TestCupt::setup('packages' => $packages, 'sources' => $sources);

eval(get_inc_code('FSE'));

sub bd_bts {
	return 'build-depends(binary-to-source(' . $_[0] . '))';
}

eis($cupt, bd_bts('Pn(a)'), qw(bd1));
eis($cupt, bd_bts('Pn(b)'), ());
eis($cupt, bd_bts('Pn(c)'), qw(bd2));
eis($cupt, bd_bts('Pn(d)'), ());
eis($cupt, bd_bts('Pn(missing)'), ());
eis($cupt, bd_bts('Pn(a)|Pn(c)'), qw(bd1 bd2));

