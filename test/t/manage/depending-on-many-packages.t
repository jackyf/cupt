use TestCupt;
use Test::More tests => 11;

use strict;
use warnings;

my @many_package_list;
my $many_package_entries;

sub generate_many_packages {
	my ($count) = @_;

	@many_package_list = ();
	$many_package_entries = '';

	for my $index (1..$count) {
		my $name = "p$index";

		push @many_package_list, $name;

		$many_package_entries .= entail(compose_package_record($name, '0'));
	}
}

sub test {
	my ($count, $command_priority, $release_is_default, $expected_result) = @_;

	generate_many_packages($count);

	my $archive = $release_is_default ? undef : 'other';
	my $cupt = TestCupt::setup(
		'packages2' =>
		[
			[
				'archive' => $archive,
				'content' => entail(compose_package_record('big', '9000') . "Depends: " . join(',', @many_package_list) . "\n") .
						$many_package_entries,
			],
		],
	);

	my $expected_version = $expected_result ? '9000' : get_unchanged_version();

	my $archive_comment = $archive // 'default';
	my $comment = "$command_priority, $archive_comment archive, depends on $count packages";

	my $cupt_options = $release_is_default ? '' : '-o apt::default-release=xyz';

	my $output = get_first_offer("$cupt -V --$command_priority $cupt_options install big -o debug::resolver=yes");
	is(get_offered_version($output, 'big'), $expected_version, $comment) or diag($output);
}

test(1, 'wish', 1 => 1);
test(5, 'wish', 1 => 1);
test(25, 'wish', 1 => 1);
test(50, 'wish', 1 => 1);
test(100, 'wish', 1 => 1);

TODO: {
	local $TODO = 'ajust score';
	test(1, 'wish', 0 => 0);
}

test(1, 'try', 0 => 1);
test(5, 'try', 0 => 1);
TODO: {
	local $TODO = 'ajust score';
	test(25, 'try', 0 => 0);
	test(50, 'try', 0 => 0);
}
test(100, 'try', 0 => 0);
		
