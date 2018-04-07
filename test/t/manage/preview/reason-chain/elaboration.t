use Test::More tests => 4;

require(get_rinclude_path('common'));

my $cupt = setup(
	'dpkg_status' => [
		compose_installed_record('aa', 1) . "Provides: foo\n" ,
		compose_installed_record('bb', 2) . "Provides: foo\n" ,
	],
);

# relation expression requests are not package-annotated
my $request = '--unsatisfy foo';
is(get_reason_chain($cupt, $request, 'aa'), "aa: user request: unsatisfy 'foo'");
is(get_reason_chain($cupt, $request, 'bb'), "bb: user request: unsatisfy 'foo'");

# version requests are package-annotated
$request = "--remove '*'";
is(get_reason_chain($cupt, $request, 'aa'), "aa: user request: remove * | for package 'aa'");
is(get_reason_chain($cupt, $request, 'bb'), "bb: user request: remove * | for package 'bb'");

