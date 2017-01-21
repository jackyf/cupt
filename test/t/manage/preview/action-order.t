use Test::More tests => 2;

my %actions = (
	'installed' => {
		'post_package' => compose_package_record('a', 6),
		'command' => 'install a',
	},
	'upgraded' => {
		'pre_package' => compose_installed_record('b', 4),
		'post_package' => compose_package_record('b', 5),
		'command' => 'install b',
	},
	'removed' => {
		'pre_package' => compose_installed_record('c', 2),
		'command' => 'remove c',
	},
);

sub extract_records {
	my ($d1, $d2, $part) = @_;
	my $result = [ $actions{$d1}->{$part}//(), $actions{$d2}->{$part}//() ];
}

sub test {
	my ($first, $second) = @_;
	my $cupt = setup(
		'dpkg_status' => extract_records($first, $second, 'pre_package'),
		'packages' => extract_records($first, $second, 'post_package'),
	);
	my @commands = map { "--$_" } @{extract_records($first, $second, 'command')};
	my $combined_command = join(' ', 'install', @commands);

	my $expected_regex = qr/$first.*\n{2,}.*$second/s;
	my $output = get_first_offer("$cupt $combined_command");
	like($output, $expected_regex, "relative order: $first -> $second");
}

test('installed', 'upgraded');
test('upgraded', 'removed');

