use TestCupt;
use Test::More tests => 12;

use strict;
use warnings;

sub extract_removed_and_purged_packages {
	my $input = shift;

	my $result = [ '', '' ];
	if ($input =~ m/will be removed:\n\n(.*) $/m) {
		$result->[0] = $1;
	}
	if ($input =~ m/will be purged:\n\n(.*) $/m) {
		$result->[1] = $1;
	}

	return $result;
}

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('aa', 0) . "Depends: bb\n") .
		entail(compose_installed_record('bb', 1). "Recommends: cc\n") .
		entail(compose_removed_record('dd')) ,
);

sub test {
	my ($comment, $subcommand, $expected_result) = @_;

	my $output = get_first_offer("$cupt $subcommand");

	is_deeply(extract_removed_and_purged_packages($output), $expected_result, $comment)
			or diag($output);
}

test('simple remove', 'remove aa' => [ 'aa', '' ]);
test('simple purge', 'purge aa' => [ '', 'aa' ]);
test('remove all', "remove '*'" => [ 'aa bb', '' ]);
test('remove --purge is equal to purge', 'remove --purge aa' => [ '', 'aa' ]);
test('remove does not touch config-files packages', 'remove aa dd' => [ 'aa', '' ]);
test('purge purges also config-files packages', 'purge aa dd' => [ '', 'aa dd' ]);
test('positional --purge', 'remove aa --purge bb' => [ 'aa', 'bb' ]);
test('positional --remove', 'purge aa --remove bb' => [ 'bb', 'aa' ]);
test('purge subcommand does not touch dependent packages', 'purge bb' => [ 'aa', 'bb' ]); 
test('apt::get::purge affects all packages', 'remove bb -o apt::get::purge=yes' => [ '', 'aa bb' ]);
test('cupt::worker::purge affects all packages', 'remove bb -o cupt::worker::purge=yes' => [ '', 'aa bb' ]);
test('purge configuration option overwrites --remove', 'purge --remove aa -o cupt::worker::purge=yes' => [ '', 'aa' ]);

