use Test::More tests => 3+5+6+6+4;

require(get_rinclude_path('common'));

my $cupt = setup();

sub re {
	my $expression = shift;
	return "regular expression '$expression' is not valid";
}

sub ucb {
	my $after = shift;
	return "unexpected closing bracket ')' after '$after'";
}

sub fcb {
	return 'too few closing brackets';
}

sub ncq {
	return "unable to find closing quoting character '/'";
}

sub lqc {
	return "the last query character is not a closing bracket ')'";
}

# TODO: better context precision

test_binary($cupt, 'maintainer(*)' => [ re('*') ]);
test_binary($cupt, 'maintainer(?)' => [ re('?') ]);
test_binary($cupt, 'maintainer(tyu[b})' => [ re('tyu[b}') ]);

test_binary($cupt, 'maintainer)' => [ ucb('maintainer') ]);
test_binary($cupt, ')' => [ ucb('') ]);
test_binary($cupt, 'abc(def) | xyz)' => [ ucb('abc(def) | xyz') ]);
test_binary($cupt, 'def(def))' => [ ucb('def(def)') ]);
test_binary($cupt, 'xyz(abc, def), )' => [ ucb('xyz(abc, def), ') ]);

test_binary($cupt, '(abc' => [ fcb() ]);
test_binary($cupt, 'abc(' => [ fcb() ]);
test_binary($cupt, 'abc(((' => [ fcb() ]);
test_binary($cupt, 'abc(()()' => [ fcb() ]);
test_binary($cupt, 'abc(((ab))' => [ fcb() ]);
test_binary($cupt, 'abc( & Pn()' => [ fcb() ]);

test_binary($cupt, '/' => [ ncq() ]);
test_binary($cupt, '///' => [ ncq() ]);
test_binary($cupt, '/abc()/def(xyz)/' => [ ncq() ]);
test_binary($cupt, 'def(/' => [ ncq() ]);
test_binary($cupt, 'def(abc, /mmm, nnn)' => [ ncq() ]);
test_binary($cupt, 'def(abc, //mmm, nnn/)' => [ ncq() ]);

test_binary($cupt, 'xyz() a' => [ lqc() ]);
test_binary($cupt, 'xyz(abc)def' => [ lqc() ]);
test_binary($cupt, 'xyz(abc, def)$' => [ lqc() ]);
test_binary($cupt, 'xyz(abc, def),' => [ lqc() ]);

