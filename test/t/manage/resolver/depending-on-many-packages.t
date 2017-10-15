use Test::More tests => 19;

sub test {
	my ($count, $command_priority, $release_is_default, $expected_result) = @_;

	my @many_package_list = map { "p$_" } (1..$count);

	my $archive = $release_is_default ? undef : 'other';
	my $cupt = setup('releases' => [{
		'archive' => $archive,
		'packages' => [
			compose_package_record('big', '9000') . "Depends: " . join(',', @many_package_list) . "\n",
			(map { compose_package_record($_, 0) } @many_package_list)
		],
	}]);

	my $expected_version = $expected_result ? '9000' : get_unchanged_version();

	my $archive_comment = $archive // 'default';
	my $comment = "$command_priority, $archive_comment archive, depends on $count packages --> $expected_result";

	my $cupt_options = $release_is_default ? '' : '-o apt::default-release=xyz';

	my $output = get_first_offer("$cupt --$command_priority $cupt_options install big -o debug::resolver=yes");
	is(get_offered_version($output, 'big'), $expected_version, $comment) or diag($output);
}

test(1, 'wish', 1 => 1);
test(5, 'wish', 1 => 1);
test(25, 'wish', 1 => 1);
test(50, 'wish', 1 => 1);
test(100, 'wish', 1 => 1);
test(300, 'wish', 1 => 1);
test(1000, 'wish', 1 => 1);

test(1, 'wish', 0 => 1);
test(3, 'wish', 0 => 1);
test(6, 'wish', 0 => 1);
test(10, 'wish', 0 => 0);
test(20, 'wish', 0 => 0);

test(1, 'try', 0 => 1);
test(5, 'try', 0 => 1);
test(25, 'try', 0 => 1);
test(100, 'try', 0 => 1);
test(200, 'try', 0 => 1);
test(300, 'try', 0 => 0);
test(500, 'try', 0 => 0);
		
