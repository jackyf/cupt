use Test::More tests => 1 + 9 + 7 + 4 + 6;

my $cupt = setup(
	'releases' => [
		{
			'archive' => 'a1',
			'codename' => 'c1',
			'packages' => [ compose_package_record('pp', 1) ],
		},
		{
			'archive' => 'a2',
			'codename' => 'c2',
			'packages' => [ compose_package_record('pp', 2) ],
		},
		{
			'archive' => 'a3',
			'codename' => 'c3',
			'packages' => [ compose_package_record('pp', 3) ],
		}
	]
);

sub test {
	my ($arguments, $expected_versions) = @_;

	my $output = stdall("$cupt show -a pp $arguments");
	my @available_versions = ($output =~ m/^Version: (.*)$/gm);
	@available_versions = sort @available_versions;
	is_deeply(\@available_versions, $expected_versions, $arguments);
}

test('# no filtering' => [1, 2, 3]);

test('--include-archives=b6' => []);
test('--include-codenames=r3,q7' => []);
test('--include-archives=a1' => [1]);
test('--include-codenames=c1' => [1]);
test('--include-archives=a2,a3' => [2, 3]);
test('--include-codenames=c3,c1' => [1, 3]);
test('--include-archives=a1,a2,a3' => [1, 2, 3]);
test('--include-codenames=c3,c2,c1' => [1, 2, 3]);
test('--include-archives=a2,a4' => [2]);

test('--exclude-archives=a4' => [1, 2, 3]);
test('--exclude-codenames=c4' => [1, 2, 3]);
test('--exclude-archives=a3' => [1, 2]);
test('--exclude-codenames=c1' => [2, 3]);
test('--exclude-archives=a2,a1' => [3]);
test('--exclude-codenames=c1,c3' => [2]);
test('--exclude-codenames=b8,c3' => [1,2]);

test('--include-archives=c1' => []);
test('--include-codenames=a1' => []);
test('--exclude-archives=c1' => [1, 2, 3]);
test('--exclude-codenames=a1' => [1, 2, 3]);

my $lr = 'cupt::cache::limit-releases';

test("-o ${lr}::by-archive::=a2 # no type specified" => [1, 2, 3]);
test("-o ${lr}::by-archive::=a2 -o ${lr}::by-archive::type=include" => [2]);
test("-o ${lr}::by-archive::=a2 -o ${lr}::by-archive::type=exclude" => [1, 3]);

test("-o ${lr}::by-codename::=c2 # no type specified" => [1, 2, 3]);
test("-o ${lr}::by-codename::=c2 -o ${lr}::by-codename::type=include" => [2]);
test("-o ${lr}::by-codename::=c2 -o ${lr}::by-codename::type=exclude" => [1, 3]);

