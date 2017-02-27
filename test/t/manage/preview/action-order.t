use Test::More tests => 11;

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
	'purged' => {
		'pre_package' => compose_installed_record('d', 9),
		'command' => 'purge d',
	},
	'downgraded' => {
		'pre_package' => compose_installed_record('e', 7),
		'post_package' => compose_package_record('e', 2),
		'command' => 'install e=2',
	},
	'configured' => {
		'pre_package' => compose_status_record('f', 'install ok unpacked', 8),
	},
	'deconfigured' => {
		'pre_package' => compose_status_record('h', 'deinstall reinstreq half-installed', 3),
	},
	'triggers' => {
		'pre_package' => compose_status_record('g', 'install ok triggers-pending', 76),
	},
	'reinstalled' => {
		'pre_package' => compose_installed_record('h', 2),
		'post_package' => compose_package_record('h', 2),
		'command' => 'reinstall h',
	},
	'marked as auto' => {
		'pre_package' => compose_installed_record('i', 1),
		'command' => 'markauto i --no-auto-remove',
	},
	'marked as manual' => {
		'pre_package' => compose_installed_record('j', 9),
		'autodb' => compose_autoinstalled_record('j'),
		'command' => 'unmarkauto j',
	},
	'not preferred' => {
		'pre_package' => compose_installed_record('k', 1),
		'post_package' => compose_package_record('k', 4),
		'command' => 'install --show-not-preferred',
	}
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
		'extended_states' => extract_records($first, $second, 'autodb'),
	);
	my @commands = map { "--$_" } @{extract_records($first, $second, 'command')};
	my $combined_command = join(' ', 'install', @commands);

	my $expected_regex = qr/ $first.*\n{2,}.* $second/s;
	my $output = get_first_offer("$cupt $combined_command");
	like($output, $expected_regex, "relative order: $first -> $second ($combined_command)");
}

test('marked as auto', 'marked as manual');
test('marked as manual', 'reinstalled');
test('reinstalled', 'installed');
test('installed', 'upgraded');
test('upgraded', 'removed');
test('removed', 'purged');
test('purged', 'downgraded');
test('downgraded', 'configured');
test('configured', 'triggers');
test('triggers', 'deconfigured');
test('deconfigured', 'not preferred');

