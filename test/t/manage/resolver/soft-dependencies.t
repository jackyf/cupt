use TestCupt;
use Test::More tests => 24;

use strict;
use warnings;

sub setup_cupt {
	my ($from, $to) = @_;

	return TestCupt::setup(
		'dpkg_status' =>
			entail(compose_installed_record('aa', 1) . "$from\n") ,
		'packages' =>
			entail(compose_package_record('aa', 2) . "$to\n") .
			entail(compose_package_record('bb', 3)) .
			entail(compose_package_record('cc', 4)) ,
	);
}

sub test {
	my ($from, $to, $bb_expected) = @_;

	my $expected_bb_version = ($bb_expected ? '3' : get_unchanged_version());

	my $cupt = setup_cupt($from, $to);
	my $options = "-o apt::install-suggests=yes -o cupt::resolver::keep-suggests=yes";
	my $output = get_first_offer("$cupt install aa=2 $options");

	my $comment = "from: '$from', to: '$to', expected: $bb_expected";
	subtest $comment => sub {
		like($output, regex_offer(), 'there is an offer');
		is(get_offered_version($output, 'bb'), $expected_bb_version, 'right bb decision')
	} or diag($output);
}

sub test_group {
	my $dep_type = shift;
	my $dep_name = ($dep_type eq 'r' ? 'Recommends' : 'Suggests');

	my $test_s = sub {
		my ($to_part, $expected) = @_;
		test("$dep_name: bb (>= 2)", "$dep_name: $to_part", $expected);
	};

	test("$dep_name: cc", "$dep_name: bb" => 1);
	test("$dep_name: bb (<< 0)", "$dep_name: bb" => 1);

	$test_s->('cc' => 0);
	$test_s->('bb (>= 4)' => 0);
	$test_s->('bb (>= 1)' => 1);
	$test_s->('bb (>= 2)' => 0);
	$test_s->('bb (>= 3)' => 1);
	$test_s->('bb' => 1);
	$test_s->('bb | cc' => 1);
	$test_s->('bb | dd' => 1);

	test("$dep_name: bb (= 1)", "$dep_name: bb (= 3)" => 1);
}

test_group('r');

test_group('s');

TODO: {
	local $TODO = 'fix or change/remove the ignoring mechanics';
	test('Recommends: bb', 'Suggests: bb' => 0);
}
test('Suggests: bb', 'Recommends: bb' => 1);

