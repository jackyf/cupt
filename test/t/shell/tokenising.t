use Test::More tests => 6+1;

require(get_rinclude_path('common'));

my $cupt = setup(
	'dpkg_status' => [ compose_installed_record('ii', 2) ],
	'packages' => [ compose_package_record('pp', 1) ],
);
my $base_command;

sub test {
	my ($command, $comment) = @_;
	subtest $comment => sub {
		test_output_identical_with_non_shell($cupt, get_shell($cupt), $command, $base_command);
	}
}

$base_command = 'show pp';
test('show pp ', 'space after parameters');
test('show   pp', 'space before parameters');
test('  show pp', 'space before command');
test("show 'pp'", 'single quotes around parameter');
test("'show' pp", 'single quotes around command');
test('show "pp"', 'double quotes');

$base_command = 'satisfy ii';
test('satisfy "ii, ii"', 'spaces within a parameter');

