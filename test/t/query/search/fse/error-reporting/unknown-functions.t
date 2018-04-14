use Test::More tests => 5+4+3+2;

require(get_rinclude_path('common'));

my $cupt = setup();

sub ubf {
	my $func = shift;
	return "unknown binary selector function '$func'";
}

sub usf {
	my $func = shift;
	return "unknown source selector function '$func'";
}


test_binary($cupt, '' => [ 'query cannot be empty' ]);
test_binary($cupt, 'zag' => [ ubf('zag') ]);
test_binary($cupt, 'zag()' => [ ubf('zag') ]);
test_binary($cupt, 'zag(yyy)' => [ ubf('zag') ]);
test_binary($cupt, 'zag($%%^^&, aBc, <>JIH)' => [ ubf('zag') ]);

test_binary($cupt, 'zig(zag())' => [ ubf('zig') ]);
test_binary($cupt, 'zig(Pn(abc))' => [ ubf('zig') ]);
test_binary($cupt, 'and(zag())' => [ ubf('zag'), upq('zag()') ]);
test_binary($cupt, 'or(or(zag(and(Pn(abc)))))' => [ ubf('zag'), upq('zag(and(Pn(abc)))'), upq('or(zag(and(Pn(abc))))') ]);

test_source($cupt, 'zug()' => [ usf('zug') ]);
test_source($cupt, 'zug(555, 777)' => [ usf('zug') ]);
test_source($cupt, 'reverse-build-depends(zeg(zug()))' => [ ubf('zeg'), upq('zeg(zug())') ]);

test_binary($cupt, 'source-to-binary(zug(555, 777))' => [ usf('zug'), upq('zug(555, 777)') ]);
test_source($cupt, 'binary-to-source(qqq())' => [ ubf('qqq'), upq('qqq()') ]);

