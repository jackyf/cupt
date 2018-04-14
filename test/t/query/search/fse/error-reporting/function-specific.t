use Test::More tests => 2+5+3+6+1;

require(get_rinclude_path('common'));

my $cupt = setup();

sub vnd {
	my $name = shift;
	return "the variable '$name' is not defined";
}

sub narg {
	my ($n) = @_;
	return "the function requires exactly $n arguments";
}

TODO: {
	local $TODO = 'make it a static check';
	test_binary($cupt, 'and(_v, _s)' => [ vnd('_v') ]);
	test_binary($cupt, 'with(_x, Pn(abc), and(_y))' => [ vnd('_y') ]);
}

test_binary($cupt, 'best()' => [ narg(1) ]);
test_binary($cupt, 'with(_x)' => [ narg(3) ]);
test_binary($cupt, 'recursive()' => [ narg(3) ]);
test_binary($cupt, 'fmap()' => [ "the function 'fmap' requires at least 2 arguments" ]);
test_binary($cupt, '_x(foo)' => [ narg(0) ]);

test_binary($cupt, 'not()' => [ narg(1) ]);
test_binary($cupt, 'xor(Pn(abc))' => [ narg(2) ]);
test_binary($cupt, 'and()' => [ 'the function should have at least one argument' ]);

test_binary($cupt, 'priority(extra, optional)' => [ narg(1) ]);
test_binary($cupt, 'essential(abc)' => [ narg(0) ]);
test_binary($cupt, 'field(Python)' => [ narg(2) ]);
test_binary($cupt, 'depends()' => [ narg(1) ]);
test_binary($cupt, 'package:installed(x)' => [ narg(0) ]);
test_binary($cupt, 'package:automatically-installed(x, y)' => [ narg(0) ]);

test_binary($cupt, 'package-with-installed-dependencies()' => [ narg(1) ]);

