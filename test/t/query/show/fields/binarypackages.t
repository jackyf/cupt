use Test::More tests => 2;

my $sources = <<END;
Package: abc
Version: 1
Binary: b1, b2, bp5555

Package: def
Version: 1
Binary: aaa,
  bbb, ccc,
  ddd
END

my $cupt = setup('sources' => $sources);

like(stdout("$cupt showsrc abc"), qr/^Binary: b1, b2, bp5555\n/m, "one-line value");
like(stdout("$cupt showsrc def"), qr/^Binary: aaa, bbb, ccc, ddd\n/m, "multi-line value");

