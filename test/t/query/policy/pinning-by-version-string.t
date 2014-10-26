use TestCupt;
use Test::More tests => 22;

sub setup_cupt {
	my ($version, $pin_expression) = @_;

	return TestCupt::setup(
		'packages2' =>
			[
				[
					'trusted' => 0,
					'content' => entail(compose_package_record('abc', $version)),
				],
			],
		'preferences' =>
			compose_version_pin_record('abc', $pin_expression, 927),
	);
}

my %match_mapper = (
	-1 => [ '', "isn't considered: broken" ],
	0  => [ 500, "doesn't match" ],
	1  => [ 927, 'matches' ],
);

sub test {
	my ($version, $pin_expression, $match_expected) = @_;

	my $cupt = setup_cupt($version, $pin_expression);

	my $output = stdall("$cupt policy abc");

	my $expected_priority = $match_mapper{$match_expected}->[0];

	my $match_comment = $match_mapper{$match_expected}->[1];
	my $comment = "'$version' $match_comment '$pin_expression'";

	is(get_version_priority($output, $version), $expected_priority, $comment)
			or diag($output);
}

test('2', '2', 1);
test('2', '3', 0);
test('20', '2', 0);
test('20', '0', 0);

test('20', '2*', 1);
test('20', '*', 1);
test('20', '3*', 0);
test('20', '?0', 1);
test('20', '?1', 0);
test('20', '2?', 1);
test('20', '??', 1);
test('20', '?', 0);

test('20', '/*/', -1);
test('20', '/.*/', 1);
test('20', '/./', 1);
test('20', '/3./', 0);
test('1.2.6', '/1\..\.6/', 1);
test('1.2.6', '/1\..\.7/', 0);
test('200', '/2+0/', 1);
test('200', '/2+1/', 0);
test('200', '/20+/', 1);
test('200', '/20++/', -1);

