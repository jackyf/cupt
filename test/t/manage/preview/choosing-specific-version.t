use Test::More tests => 3;

my $cupt = setup(
	'packages' => [
		compose_package_record('pp', 5) . "Depends: not-existing\n" ,
		compose_package_record('pp', 5, 'sha' => 'abc1') ,
		compose_package_record('pp', 5, 'sha' => 'abc2') . "Recommends: not-existing\n" ,
		compose_package_record('pp', '4.22-1~bpo8+1') ,
	],
);

sub test {
	my ($comment, $request, $expected_version) = @_;

	my $output = get_first_offer("$cupt install --sf $request");
	my $actual_version = get_offered_version($output, 'pp');

	is($actual_version, $expected_version, $comment)
			or (diag($output), diag(stdall("$cupt policy pp")));
}

test('best available dhs version', 'pp', '5^dhs0');
test('specifictly chosen dhs version', 'pp=5^dhs1', '5^dhs1');

test('regex characters are escaped', 'pp=4.22-1~bpo8+1', '4.22-1~bpo8+1');

