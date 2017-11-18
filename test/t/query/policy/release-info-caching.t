use Test::More tests => 6*4;

sub get_message_counts {
	my ($output) = @_;

	my %result;

	while ($output =~ m/(?:the release|gpg:|present for) '\S* (\S*)'/g) {
		$result{$1}++;
	}

	return \%result;
}

sub test_group {
	my ($text_prefix, $release_attributes, $arguments, $expected_message_kind_regex) = @_;

	my $test = sub {
		my ($text, $releases, $expected_message_counts) = @_;

		my @adapted_releases = @$releases;
		for (@adapted_releases) {
			$_ = { %$_, %$release_attributes };
		}

		my $cupt = setup('releases' => \@adapted_releases);

		subtest "$text_prefix: $text" => sub {
			# showsrc forces parsing both source and binary archives
			my $output = stdall("$cupt showsrc '*' $arguments"); 

			like($output, $expected_message_kind_regex, 'message kind');
			is_deeply(get_message_counts($output), $expected_message_counts, 'message counts')
					or diag($output);
		};
	};

	$test->('no caching needed',
		[{ 'archive' => 'aa', 'packages' => [] }],
		{'aa' => 1});

	$test->('several components',
		[{
			'archive' => 'aa',
			'components' => [
				{ 'component' => 'x', 'packages' => [] },
				{ 'component' => 'y', 'packages' => [] },
				{ 'component' => 'z', 'packages' => [] },
			],
		}],
		{'aa' => 1});

	$test->('sources and packages',
		[{ 'archive' => 'bb', 'packages' => [], 'sources' => [] }],
		{'bb' => 1});

	$test->('components having sources and packages',
		[{
			'archive' => 'cc',
			'components' => [
				{ 'component' => 'x', 'sources' => [], 'packages' => []},
				{ 'component' => 'y', 'sources' => [], 'packages' => []},
			],
		}],
		{'cc' => 1});

	$test->('different archives have different messages',
		[
			{ 'archive' => 'dd', 'packages' => [] },
			{ 'archive' => 'ee', 'packages' => [] },
		],
		{'dd' => 1, 'ee' => 1});

	$test->('different URIs have different messages',
		[
			{ 'archive' => 'ff', 'scheme' => 'http', 'packages' => [] },
			{ 'archive' => 'ff', 'scheme' => 'ftp', 'packages' => [] },
		],
		{'ff' => 2});
}

test_group('unsigned', { 'trusted' => 'check' }, '' => qr/empty signature/);

my $too_old_date = 'Mon, 07 Oct 2013 14:44:53 UTC';
test_group('too old (error)', {'valid-until' => $too_old_date},
		'' => qr/E: .*expire/);
test_group('too old (warning)', {'valid-until' => $too_old_date},
		'-o cupt::cache::release-file-expiration::ignore=yes' => qr/W: .*expire/);

test_group('file missing', { 'hooks' => { 'sign' => { 'write' => sub { return undef; } } } },
		'' => qr/no release file present/);

